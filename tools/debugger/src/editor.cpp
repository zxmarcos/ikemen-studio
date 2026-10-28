/*
 * The code editor of the debugger: a text editor widget for the files of a
 * character (CNS, CMD, AIR, DEF).
 *
 * With a column of marks and
 * line backgrounds added for the Code window. It edits the caller's
 * std::string in place, draws only the lines in view, colours them as MUGEN
 * text, completes and documents the names of the triggers and the state
 * controllers. While it has the keyboard focus it holds the ImGui active id
 * with every keyboard key, like InputText, so io.WantCaptureKeyboard and
 * io.WantTextInput keep the keys away from the game.
 */
#include "editor.hpp"

#include <imgui_internal.h>

#include <math.h>
#include <string.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>

namespace dbg {

/* ------------------------------------------------------------------ the names it knows */

struct NameDoc {
    const char *name;
    const char *syntax;     /* triggers: how it is written; controllers: the main parameters */
    const char *desc;
};

static const NameDoc kTriggers[] = {
    { "Abs", "Abs(exp)", "absolute value of exp" },
    { "ACos", "ACos(exp)", "arc cosine of exp, in radians" },
    { "AILevel", "AILevel", "AI difficulty level of the player (1-8), 0 when a human controls it" },
    { "Alive", "Alive", "1 while the player is not KO" },
    { "Anim", "Anim", "action number of the current animation" },
    { "AnimElem", "AnimElem = n  /  AnimElem = n, >= t",
      "true on the tick element n of the current animation starts; with a second comparison, compares the ticks since it started" },
    { "AnimElemNo", "AnimElemNo(t)", "number of the element of the current animation shown t ticks from now (0: now)" },
    { "AnimElemTime", "AnimElemTime(n)", "ticks since element n of the current animation started (negative before)" },
    { "AnimExist", "AnimExist(n)", "1 if action n exists in the animations the player uses now" },
    { "AnimTime", "AnimTime", "ticks to the end of the current animation, negative before it ends (0 on its last tick)" },
    { "ASin", "ASin(exp)", "arc sine of exp, in radians" },
    { "ATan", "ATan(exp)", "arc tangent of exp, in radians" },
    { "AuthorName", "AuthorName = \"name\"", "the author of the character (only = and !=)" },
    { "BackEdgeBodyDist", "BackEdgeBodyDist", "distance from the back of the player's body (its width) to the back edge of the screen" },
    { "BackEdgeDist", "BackEdgeDist", "distance from the player's axis to the back edge of the screen" },
    { "CanRecover", "CanRecover", "1 if the falling player can recover now (fall.recover and fall.recovertime of the hit)" },
    { "Ceil", "Ceil(exp)", "smallest integer not below exp" },
    { "Command", "Command = \"name\"", "true while the command name of the CMD is detected (only = and !=)" },
    { "Const", "Const(name)",
      "a constant of the character: data.life, data.power, data.attack, data.defence, data.fall.defence_mul, "
      "data.liedown.time, data.airjuggle, data.sparkno, data.guard.sparkno, data.ko.echo, data.intpersistindex, "
      "data.floatpersistindex, size.xscale, size.yscale, size.ground.back, size.ground.front, size.air.back, "
      "size.air.front, size.height, size.attack.dist, size.proj.attack.dist, size.proj.doscale, size.head.pos.x/y, "
      "size.mid.pos.x/y, size.shadowoffset, size.draw.offset.x/y, velocity.walk.fwd.x, velocity.walk.back.x, "
      "velocity.run.fwd.x/y, velocity.run.back.x/y, velocity.jump.y, velocity.jump.neu.x, velocity.jump.fwd.x, "
      "velocity.jump.back.x, velocity.runjump.fwd.x, velocity.runjump.back.x, velocity.airjump.y, "
      "velocity.airjump.neu.x, velocity.airjump.fwd.x, velocity.airjump.back.x, velocity.air.gethit.groundrecover.x/y, "
      "velocity.air.gethit.airrecover.mul.x/y, velocity.air.gethit.airrecover.add.x/y, movement.airjump.num, "
      "movement.airjump.height, movement.yaccel, movement.stand.friction, movement.crouch.friction, "
      "movement.stand.friction.threshold, movement.crouch.friction.threshold, movement.air.gethit.groundlevel, "
      "movement.air.gethit.airrecover.threshold, movement.air.gethit.airrecover.yaccel, movement.down.bounce.yaccel, "
      "movement.down.friction.threshold" },
    { "Cos", "Cos(exp)", "cosine of exp (radians)" },
    { "Ctrl", "Ctrl", "1 if the player has control" },
    { "DrawGame", "DrawGame", "1 if the round ended in a draw" },
    { "E", "E", "Euler's number, 2.71828..." },
    { "Exp", "Exp(exp)", "e raised to the power exp" },
    { "FrontEdgeBodyDist", "FrontEdgeBodyDist", "distance from the front of the player's body to the front edge of the screen" },
    { "FrontEdgeDist", "FrontEdgeDist", "distance from the player's axis to the front edge of the screen" },
    { "Floor", "Floor(exp)", "largest integer not above exp" },
    { "FVar", "FVar(n)", "float variable n (0-39)" },
    { "GameHeight", "GameHeight", "height of the game space, in the player's coordinates" },
    { "GameTime", "GameTime", "ticks the game has run" },
    { "GameWidth", "GameWidth", "width of the game space, in the player's coordinates" },
    { "GetHitVar", "GetHitVar(name)",
      "a value of the last hit taken: xveladd, yveladd, type, animtype, airtype, groundtype, damage, hitcount, "
      "fallcount, hitshaketime, hittime, slidetime, ctrltime, recovertime, xoff, yoff, xvel, yvel, yaccel, chainid, "
      "guarded, isbound, fall, fall.damage, fall.xvel, fall.yvel, fall.recover, fall.time, fall.recovertime, "
      "fall.kill, fall.envshake.time, fall.envshake.freq, fall.envshake.ampl, fall.envshake.phase" },
    { "HitCount", "HitCount", "hits the current attack move has done (one per hit, however many opponents it touched)" },
    { "HitDefAttr", "HitDefAttr = state, attr", "compares the attributes of the active HitDef, e.g. HitDefAttr = SC, NA, SA (only = and !=)" },
    { "HitFall", "HitFall", "1 if the player, hit, is falling" },
    { "HitOver", "HitOver", "1 when the hit time of the last hit is over" },
    { "HitPauseTime", "HitPauseTime", "ticks of hit pause left after the player's attack made contact" },
    { "HitShakeOver", "HitShakeOver", "1 when the hit shake of the last hit is over" },
    { "HitVel", "HitVel X  /  HitVel Y", "velocity given by the last hit" },
    { "ID", "ID", "unique ID of the player" },
    { "IfElse", "IfElse(cond, a, b)", "a if cond is not 0, else b (both are evaluated)" },
    { "InGuardDist", "InGuardDist", "1 if the player is inside the guard distance of an opponent's attack or projectile" },
    { "IsHelper", "IsHelper  /  IsHelper(id)", "1 if the player is a helper (with this helper ID)" },
    { "IsHomeTeam", "IsHomeTeam", "1 if the player's team is the home team" },
    { "Life", "Life", "life of the player" },
    { "LifeMax", "LifeMax", "maximum life of the player" },
    { "Ln", "Ln(exp)", "natural logarithm of exp" },
    { "Log", "Log(base, exp)", "logarithm of exp in base base" },
    { "Lose", "Lose", "1 if the player's team lost the round" },
    { "LoseKO", "LoseKO", "1 if the player's team lost the round by KO" },
    { "LoseTime", "LoseTime", "1 if the player's team lost the round by time over" },
    { "MatchNo", "MatchNo", "number of the current match (1: first)" },
    { "MatchOver", "MatchOver", "1 if the match has ended" },
    { "MoveContact", "MoveContact", "not 0 once the current attack hit or was guarded (0 if it missed)" },
    { "MoveGuarded", "MoveGuarded", "not 0 once the current attack was guarded" },
    { "MoveHit", "MoveHit", "not 0 once the current attack hit" },
    { "MoveReversed", "MoveReversed", "not 0 once the current attack was reversed by a ReversalDef" },
    { "MoveType", "MoveType", "A attack, I idle, H being hit (only = and !=)" },
    { "Name", "Name = \"name\"", "the name of the character (only = and !=)" },
    { "NumEnemy", "NumEnemy", "number of opponents" },
    { "NumExplod", "NumExplod  /  NumExplod(id)", "number of explods of the player (with this ID)" },
    { "NumHelper", "NumHelper  /  NumHelper(id)", "number of helpers of the player (with this helper ID)" },
    { "NumPartner", "NumPartner", "number of partners of the player" },
    { "NumProj", "NumProj", "number of projectiles of the player" },
    { "NumProjID", "NumProjID(id)", "number of projectiles of the player with this projectile ID" },
    { "NumTarget", "NumTarget  /  NumTarget(id)", "number of targets of the player (hit by the HitDef with this id)" },
    { "P1Name", "P1Name = \"name\"", "name of the player itself (same as Name)" },
    { "P2BodyDist", "P2BodyDist X  /  P2BodyDist Y", "distance to the nearest opponent between the fronts of the bodies (x) or the tops (y)" },
    { "P2Dist", "P2Dist X  /  P2Dist Y", "distance from the player's axis to the nearest opponent's (x positive in front)" },
    { "P2Life", "P2Life", "life of the nearest opponent" },
    { "P2MoveType", "P2MoveType", "move type of the nearest opponent: A, I, H" },
    { "P2Name", "P2Name = \"name\"", "name of the nearest opponent" },
    { "P2StateNo", "P2StateNo", "state number of the nearest opponent" },
    { "P2StateType", "P2StateType", "state type of the nearest opponent: S, C, A, L" },
    { "P3Name", "P3Name = \"name\"", "name of the player's partner" },
    { "P4Name", "P4Name = \"name\"", "name of the opponent's partner" },
    { "PalNo", "PalNo", "palette number of the player (1-12)" },
    { "ParentDist", "ParentDist X  /  ParentDist Y", "distance from the helper to its parent" },
    { "Pi", "Pi", "3.14159..." },
    { "PlayerIDExist", "PlayerIDExist(id)", "1 if a player with this ID exists" },
    { "Pos", "Pos X  /  Pos Y", "position: x from the center of the screen, y from the floor (negative up)" },
    { "Power", "Power", "power of the player" },
    { "PowerMax", "PowerMax", "maximum power of the player" },
    { "PrevStateNo", "PrevStateNo", "number of the state the player was in before the current one" },
    { "ProjCancelTime", "ProjCancelTime(id)", "ticks since a projectile of the player with this ID (0: any) was cancelled, -1 if none" },
    { "ProjContact", "ProjContact[id] = 1  /  ProjContact[id] = 1, < t",
      "true if a projectile of the player (with this ID) hit or was guarded (see ProjContactTime)" },
    { "ProjContactTime", "ProjContactTime(id)", "ticks since a projectile of the player with this ID (0: any) hit or was guarded, -1 if none" },
    { "ProjGuarded", "ProjGuarded[id] = 1  /  ProjGuarded[id] = 1, < t", "true if a projectile of the player (with this ID) was guarded" },
    { "ProjGuardedTime", "ProjGuardedTime(id)", "ticks since a projectile of the player with this ID (0: any) was guarded, -1 if none" },
    { "ProjHit", "ProjHit[id] = 1  /  ProjHit[id] = 1, < t", "true if a projectile of the player (with this ID) hit" },
    { "ProjHitTime", "ProjHitTime(id)", "ticks since a projectile of the player with this ID (0: any) hit, -1 if none" },
    { "Random", "Random", "a random number from 0 to 999, new each time it is evaluated" },
    { "RootDist", "RootDist X  /  RootDist Y", "distance from the helper to its root" },
    { "RoundNo", "RoundNo", "number of the current round (1: first)" },
    { "RoundsExisted", "RoundsExisted", "rounds the player has existed in before this one" },
    { "RoundState", "RoundState", "0 pre-intro, 1 intro, 2 fight, 3 pre-over (just KO or time over), 4 over" },
    { "ScreenPos", "ScreenPos X  /  ScreenPos Y", "position relative to the top-left corner of the screen" },
    { "SelfAnimExist", "SelfAnimExist(n)", "1 if action n exists in the player's own AIR file" },
    { "Sin", "Sin(exp)", "sine of exp (radians)" },
    { "StageVar", "StageVar(name) = \"value\"", "information on the stage: info.name, info.displayname, info.author (only = and !=)" },
    { "StateNo", "StateNo", "number of the current state" },
    { "StateType", "StateType", "S stand, C crouch, A air, L lying down (only = and !=)" },
    { "SysFVar", "SysFVar(n)", "system float variable n (0-4), for the common states" },
    { "SysVar", "SysVar(n)", "system int variable n (0-4), for the common states" },
    { "Tan", "Tan(exp)", "tangent of exp (radians)" },
    { "TeamMode", "TeamMode", "Single, Simul or Turns (only = and !=)" },
    { "TeamSide", "TeamSide", "1 or 2: the side of the player's team" },
    { "TicksPerSecond", "TicksPerSecond", "ticks per second of the game (60 by default)" },
    { "Time", "Time", "ticks since the player entered the current state (0 on the first tick)" },
    { "TimeMod", "TimeMod = m, r", "true when Time % m = r" },
    { "UniqHitCount", "UniqHitCount", "hits the current attack move has done, counting each opponent hit (two opponents: two)" },
    { "Var", "Var(n)", "int variable n (0-59)" },
    { "Vel", "Vel X  /  Vel Y", "velocity of the player (x positive forward, y positive down)" },
    { "Win", "Win", "1 if the player's team won the round" },
    { "WinKO", "WinKO", "1 if the player's team won the round by KO" },
    { "WinPerfect", "WinPerfect", "1 if the player's team won the round without losing life" },
    { "WinTime", "WinTime", "1 if the player's team won the round by time over" },
};

static const NameDoc kRedirects[] = {
    { "Parent", "Parent, trigger", "evaluates trigger for the parent of the helper (the player who made it)" },
    { "Root", "Root, trigger", "evaluates trigger for the root of the helper (the player at the top of its family)" },
    { "Helper", "Helper(id), trigger", "evaluates trigger for the first helper with this helper ID (any helper without id)" },
    { "Target", "Target(id), trigger", "evaluates trigger for the first target (hit by the HitDef with this id)" },
    { "Partner", "Partner, trigger", "evaluates trigger for the player's partner" },
    { "Enemy", "Enemy(n), trigger", "evaluates trigger for opponent n (0: the first)" },
    { "EnemyNear", "EnemyNear(n), trigger", "evaluates trigger for the n-th nearest opponent (0: the nearest)" },
    { "PlayerID", "PlayerID(id), trigger", "evaluates trigger for the player with this ID" },
    { "P2", "P2, trigger", "evaluates trigger for the nearest opponent" },
    { "StateOwner", "StateOwner, trigger", "evaluates trigger for the owner of the state the player is in (a custom state's giver)" },
    { "Player", "Player(n), trigger", "evaluates trigger for player n (1: P1)" },
    { "PlayerIndex", "PlayerIndex(n), trigger", "evaluates trigger for the n-th player in the game's list" },
    { "HelperIndex", "HelperIndex(n), trigger", "evaluates trigger for the player's n-th helper" },
};

static const char kPalFX[] = "time = t; add = r,g,b; mul = r,g,b; sinadd = r,g,b,period; invertall = 0/1; color = 0-256";

static const NameDoc kControllers[] = {
    { "AfterImage", "time = t; length = n; palcolor = c; palinvertall = 0/1; palbright = r,g,b; palcontrast = r,g,b; "
      "palpostbright = r,g,b; paladd = r,g,b; palmul = r,g,b; timegap = t; framegap = n; trans = add/add1/sub/none",
      "leaves a trail of images behind the player" },
    { "AfterImageTime", "time = t", "changes how long the current afterimage lasts (0 ends it)" },
    { "AllPalFX", kPalFX, "PalFX on everything on the screen (stage, players, lifebars)" },
    { "AngleAdd", "value = degrees", "adds to the angle AngleDraw draws with" },
    { "AngleDraw", "value = degrees; scale = x, y", "draws the player rotated (and scaled) this tick" },
    { "AngleMul", "value = factor", "multiplies the angle AngleDraw draws with" },
    { "AngleSet", "value = degrees", "sets the angle AngleDraw draws with" },
    { "AppendToClipboard", "text = \"format\"; params = a, b, ...", "appends formatted text (%d %f \\n) to the debug clipboard" },
    { "AssertSpecial", "flag = name; flag2 = name; flag3 = name",
      "sets special flags for this tick: intro, invisible, roundnotover, nobardisplay, nobg, nofg, nostandguard, "
      "nocrouchguard, noairguard, noautoturn, nojugglecheck, nokosnd, nokoslow, noshadow, globalnoshadow, nomusic, "
      "nowalk, timerfreeze, unguardable" },
    { "AttackDist", "value = distance", "changes the guard distance of the current HitDef" },
    { "AttackMulSet", "value = factor", "sets the multiplier of the damage the player deals" },
    { "BGPalFX", kPalFX, "PalFX on the stage" },
    { "BindToParent", "time = t; facing = -1/0/1; pos = x, y", "keeps the helper at its parent's position" },
    { "BindToRoot", "time = t; facing = -1/0/1; pos = x, y", "keeps the helper at its root's position" },
    { "BindToTarget", "time = t; id = hitdef id; pos = x, y, Foot/Mid/Head", "keeps the player at a target's position" },
    { "ChangeAnim", "value = action; elem = n", "changes the animation of the player" },
    { "ChangeAnim2", "value = action; elem = n", "changes the animation, taken from the AIR of the state's owner (custom states)" },
    { "ChangeState", "value = state; ctrl = 0/1; anim = action", "changes to another state" },
    { "ClearClipboard", "", "clears the debug clipboard" },
    { "CtrlSet", "value = 0/1", "gives or takes the control of the player" },
    { "DefenceMulSet", "value = factor", "sets the multiplier of the damage the player takes" },
    { "DestroySelf", "recursive = 0/1; removeexplods = 0/1", "removes the helper (its own helpers with recursive, its explods with removeexplods)" },
    { "DisplayToClipboard", "text = \"format\"; params = a, b, ...", "replaces the debug clipboard with formatted text (%d %f \\n)" },
    { "EnvColor", "value = r,g,b; time = t; under = 0/1", "fills the screen with a colour (under: behind the players)" },
    { "EnvShake", "time = t; freq = f; ampl = a; phase = p", "shakes the screen vertically" },
    { "Explod", "anim = [F]action; id = n; pos = x, y; postype = p1/p2/front/back/left/right; facing = 1/-1; "
      "vfacing = 1/-1; bindtime = t; vel = x, y; accel = x, y; random = x, y; removetime = t; supermovetime = t; "
      "pausemovetime = t; scale = x, y; sprpriority = n; ontop = 0/1; shadow = r,g,b; ownpal = 0/1; "
      "removeongethit = 0/1; ignorehitpause = 0/1; trans = type",
      "shows an animation (explod) on the screen" },
    { "ExplodBindTime", "id = n; time = t", "changes how long the explods with this ID stay bound to the player" },
    { "FallEnvShake", "", "shakes the screen with the fall.envshake of the hit taken" },
    { "ForceFeedback", "waveform = sine/square/sinesquare/off; time = t; freq = f; ampl = a; self = 0/1", "rumbles the pad" },
    { "GameMakeAnim", "value = action; under = 0/1; pos = x, y; random = n", "plays an animation of fightfx (old, Explod with F is better)" },
    { "Gravity", "", "adds the yaccel constant to the y velocity" },
    { "Helper", "helpertype = normal/player; name = \"name\"; id = n; pos = x, y; postype = p1/p2/front/back/left/right; "
      "facing = 1/-1; stateno = state; keyctrl = 0/1; ownpal = 0/1; supermovetime = t; pausemovetime = t; size.* = ...",
      "makes a helper that runs the player's states" },
    { "HitAdd", "value = n", "adds hits to the combo counter" },
    { "HitBy", "value = attr (or value2 = attr); time = t", "the player can only be hit by these attributes for a time" },
    { "HitDef", "attr = SCA, NA/SA/HA/NT/ST/HT/NP/SP/HP; damage = hit, guard; animtype = light/medium/hard/back/up/diagup; "
      "guardflag = HLA; hitflag = HLAFD+-; pausetime = p1, p2; sparkno = n; sparkxy = x, y; hitsound = [S]g, n; "
      "guardsound = [S]g, n; ground.type = high/low/trip; ground.slidetime = t; ground.hittime = t; "
      "ground.velocity = x, y; air.velocity = x, y; fall = 0/1; p1stateno = n; p2stateno = n; ...",
      "defines the attack of the player in this state" },
    { "HitFallDamage", "", "applies the fall damage of the last hit" },
    { "HitFallSet", "value = -1/0/1; xvel = x; yvel = y", "sets whether the player falls from the last hit, and the fall velocity" },
    { "HitFallVel", "", "applies the fall velocity of the last hit" },
    { "HitOverride", "attr = SCA, NA...; stateno = state; slot = 0-7; time = t; forceair = 0/1",
      "goes to stateno instead of being hit by these attributes" },
    { "HitVelSet", "x = 0/1; y = 0/1", "sets the velocity from the last hit (old)" },
    { "LifeAdd", "value = n; kill = 0/1; absolute = 0/1", "adds life (scaled by the defence unless absolute)" },
    { "LifeSet", "value = n", "sets the life" },
    { "MakeDust", "pos = x, y; pos2 = x, y; spacing = n", "makes dust (old, Explod is better)" },
    { "ModifyExplod", "id = n; and any Explod parameter", "changes the explods with this ID" },
    { "MoveHitReset", "", "resets MoveHit, MoveGuarded and MoveContact to 0" },
    { "NotHitBy", "value = attr (or value2 = attr); time = t", "the player cannot be hit by these attributes for a time" },
    { "Null", "", "does nothing (its triggers can still assign variables with :=)" },
    { "Offset", "x = n; y = n", "moves where the player is drawn, this tick" },
    { "PalFX", kPalFX, "changes the colours of the player for a time" },
    { "ParentVarAdd", "v = n (or fv = n); value = x (or var(n) = x)", "adds to a variable of the helper's parent" },
    { "ParentVarSet", "v = n (or fv = n); value = x (or var(n) = x)", "sets a variable of the helper's parent" },
    { "Pause", "time = t; endcmdbuftime = t; movetime = t; pausebg = 0/1", "pauses the game (the player still moves for movetime)" },
    { "PlayerPush", "value = 0/1", "turns the pushing between players off for this tick" },
    { "PlaySnd", "value = [F]group, number; channel = n; lowpriority = 0/1; freq = f; loop = 0/1; pan = p; abspan = p; volumescale = %",
      "plays a sound" },
    { "PosAdd", "x = n; y = n", "moves the player" },
    { "PosFreeze", "value = 0/1", "keeps the player in place this tick" },
    { "PosSet", "x = n; y = n", "sets the position of the player" },
    { "PowerAdd", "value = n", "adds power" },
    { "PowerSet", "value = n", "sets the power" },
    { "Projectile", "projid = n; projanim = action; projhitanim; projremanim; projcancelanim; projscale = x, y; projremove = 0/1; "
      "projremovetime = t; velocity = x, y; remvelocity = x, y; accel = x, y; velmul = x, y; projhits = n; projmisstime = t; "
      "projpriority = n; projsprpriority = n; projedgebound; projstagebound; projheightbound = lo, hi; offset = x, y; "
      "postype; projshadow; supermovetime; pausemovetime; afterimage.*; and the HitDef parameters",
      "fires a projectile" },
    { "RemapPal", "source = group, no; dest = group, no", "replaces a palette by another" },
    { "RemoveExplod", "id = n", "removes the explods with this ID (all of them without id)" },
    { "ReversalDef", "reversal.attr = SCA, NA...; pausetime = p1, p2; sparkno; sparkxy; hitsound; p1stateno; p2stateno; ...",
      "reverses the attacks with these attributes" },
    { "ScreenBound", "value = 0/1; movecamera = x, y", "whether the player is kept on the screen (and moves the camera), this tick" },
    { "SelfState", "value = state; ctrl = 0/1; anim = action", "changes to a state of the player's own (leaves a custom state)" },
    { "SndPan", "channel = n; pan = p (or abspan = p)", "changes the panning of a sound that is playing" },
    { "SprPriority", "value = n", "sets the drawing priority of the player (higher is in front)" },
    { "StateTypeSet", "statetype = S/C/A/L; movetype = I/A/H; physics = S/C/A/N", "changes the state type, move type and physics" },
    { "StopSnd", "channel = n (-1: all)", "stops a sound" },
    { "SuperPause", "time = t; anim = [S]action; sound = [S]group, no; pos = x, y; darken = 0/1; p2defmul = f; poweradd = n; "
      "unhittable = 0/1; movetime = t; endcmdbuftime = t",
      "freezes the game for a super move, darkening the screen" },
    { "TargetBind", "time = t; id = hitdef id; pos = x, y", "keeps the targets at a position relative to the player" },
    { "TargetDrop", "excludeid = id; keepone = 0/1", "lets go of the targets except those of excludeid" },
    { "TargetFacing", "value = 1/-1; id = hitdef id", "turns the targets the same way as the player (1) or the other way (-1)" },
    { "TargetLifeAdd", "value = n; id = hitdef id; kill = 0/1; absolute = 0/1", "adds life to the targets" },
    { "TargetPowerAdd", "value = n; id = hitdef id", "adds power to the targets" },
    { "TargetState", "value = state; id = hitdef id", "puts the targets into a state of the player (custom state)" },
    { "TargetVelAdd", "x = n; y = n; id = hitdef id", "adds to the velocity of the targets" },
    { "TargetVelSet", "x = n; y = n; id = hitdef id", "sets the velocity of the targets" },
    { "Trans", "trans = default/none/add/addalpha/add1/sub; alpha = src, dst", "changes the transparency of the player, this tick" },
    { "Turn", "", "turns the player around" },
    { "VarAdd", "v = n (or fv = n); value = x (or var(n) = x / fvar(n) = x)", "adds to a variable" },
    { "VarRandom", "v = n; range = min, max", "sets an int variable to a random number" },
    { "VarRangeSet", "first = n; last = n; value = x (or fvalue = x)", "sets a range of variables" },
    { "VarSet", "v = n (or fv = n); value = x (or var(n) = x / fvar(n) = x / sysvar(n) = x)", "sets a variable" },
    { "VelAdd", "x = n; y = n", "adds to the velocity" },
    { "VelMul", "x = f; y = f", "multiplies the velocity" },
    { "VelSet", "x = n; y = n", "sets the velocity" },
    { "VictoryQuote", "value = n", "chooses the victory quote shown if the player wins" },
    { "Width", "edge = front, back; player = front, back (or value = front, back)", "changes the width of the player, this tick" },
};

/* Parameters and other words worth completing (no documentation). */
static const char *const kParams[] = {
    "value", "value2", "ctrl", "anim", "attr", "damage", "animtype", "air.animtype", "fall.animtype", "guardflag",
    "hitflag", "affectteam", "pausetime", "guard.pausetime", "sparkno", "guard.sparkno", "sparkxy", "hitsound",
    "guardsound", "ground.type", "air.type", "ground.slidetime", "guard.slidetime", "ground.hittime",
    "guard.hittime", "air.hittime", "guard.ctrltime", "guard.dist", "yaccel", "ground.velocity", "guard.velocity",
    "air.velocity", "airguard.velocity", "ground.cornerpush.veloff", "air.cornerpush.veloff",
    "down.cornerpush.veloff", "guard.cornerpush.veloff", "airguard.cornerpush.veloff", "airguard.ctrltime",
    "air.juggle", "mindist", "maxdist", "snap", "p1sprpriority", "p2sprpriority", "p1facing", "p1getp2facing",
    "p2facing", "p1stateno", "p2stateno", "p2getp1state", "forcestand", "fall", "fall.xvelocity",
    "fall.yvelocity", "fall.recover", "fall.recovertime", "fall.damage", "air.fall", "forcenofall",
    "down.velocity", "down.hittime", "down.bounce", "id", "chainid", "nochainid", "hitonce", "kill", "guard.kill",
    "fall.kill", "numhits", "getpower", "givepower", "palfx.time", "palfx.mul", "palfx.add", "palfx.sinadd",
    "palfx.invertall", "palfx.color", "envshake.time", "envshake.freq", "envshake.ampl", "envshake.phase",
    "fall.envshake.time", "fall.envshake.freq", "fall.envshake.ampl", "fall.envshake.phase", "x", "y", "v", "fv",
    "persistent", "ignorehitpause", "triggerall", "trigger1", "trigger2", "trigger3", "trigger4", "trigger5",
    "trigger6", "trigger7", "trigger8", "trigger9", "type", "movetype", "physics", "juggle", "velset", "poweradd",
    "sprpriority", "facep2", "hitdefpersist", "movehitpersist", "hitcountpersist", "statetype", "time", "length",
    "palcolor", "palinvertall", "palbright", "palcontrast", "palpostbright", "paladd", "palmul", "timegap",
    "framegap", "trans", "alpha", "postype", "pos", "vel", "accel", "random", "removetime", "scale", "bindtime",
    "supermove", "supermovetime", "pausemovetime", "ontop", "shadow", "ownpal", "removeongethit", "facing",
    "vfacing", "helpertype", "name", "stateno", "keyctrl", "projid", "projanim", "projhitanim", "projremanim",
    "projcancelanim", "projscale", "projremove", "projremovetime", "velocity", "remvelocity", "velmul", "projhits",
    "projmisstime", "projpriority", "projsprpriority", "projedgebound", "projstagebound", "projheightbound",
    "offset", "projshadow", "channel", "lowpriority", "freq", "loop", "pan", "abspan", "volumescale", "darken",
    "p2defmul", "unhittable", "sound", "endcmdbuftime", "movetime", "pausebg", "flag", "flag2", "flag3", "slot",
    "forceair", "text", "params", "source", "dest", "range", "first", "last", "fvalue", "edge", "player",
    "waveform", "ampl", "phase", "self", "under", "color", "add", "mul", "sinadd", "invertall", "elem", "absolute",
    "recursive", "removeexplods", "excludeid", "keepone", "movecamera", "reversal.attr", "spacing", "pos2", "xvel",
    "yvel", "priority", "size.xscale", "size.yscale", "size.ground.back", "size.ground.front", "size.air.back",
    "size.air.front", "size.height", "size.proj.doscale", "size.head.pos", "size.mid.pos", "size.shadowoffset",
    "afterimage.time", "afterimage.length", "command", "buffer.time", "data.life", "data.power", "data.attack",
    "data.defence", "data.fall.defence_mul", "data.liedown.time", "data.airjuggle", "velocity.walk.fwd.x",
    "velocity.walk.back.x", "velocity.run.fwd.x", "velocity.run.back.x", "velocity.run.back.y", "velocity.jump.y",
    "velocity.jump.neu.x", "velocity.jump.fwd.x", "velocity.jump.back.x", "velocity.runjump.fwd.x",
    "velocity.airjump.y", "movement.yaccel", "movement.stand.friction", "movement.crouch.friction",
    "movement.airjump.num", "movement.airjump.height", "xveladd", "yveladd", "hittime", "slidetime", "ctrltime",
    "recovertime", "hitshaketime", "hitcount", "fallcount", "groundtype", "airtype", "guarded", "isbound",
    "fall.time", "displayname", "versiondate", "mugenversion", "author", "pal.defaults", "localcoord", "cmd", "cns",
    "stcommon", "sprite", "ai", "intro.storyboard", "ending.storyboard",
};

/* Words of the AIR files. */
static const char *const kKeywords[] = { "loopstart", "interpolate" };

/* The words of ZSS's statements. */
static const char *const kZssKeywords[] = {
    "if", "else", "switch", "case", "default", "for", "while", "break", "continue", "let", "call", "ignorehitpause",
    "persistent",
};

/* What the parameters most controllers share mean (a controller's own line in kControllers says more). */
static const NameDoc kParamDocs[] = {
    { "value", "", "the controller's main value" },
    { "value2", "", "a second value" },
    { "ctrl", "0/1", "gives (1) or takes (0) the control of the player" },
    { "anim", "action", "an action of the AIR file (F prefix: fightfx's)" },
    { "elem", "n", "the element of the action to start from" },
    { "x", "n", "the x component (forward from the player's facing)" },
    { "y", "n", "the y component (positive down)" },
    { "z", "n", "the z component (depth)" },
    { "time", "ticks", "how long it lasts (-1: forever, where it applies)" },
    { "id", "n", "an ID, to find it again (explods, helpers, projectiles, HitDef targets)" },
    { "pos", "x, y", "a position, relative to what postype says" },
    { "postype", "p1/p2/front/back/left/right", "what pos is relative to" },
    { "facing", "1/-1", "the same way as the player (1) or the other way (-1)" },
    { "vfacing", "1/-1", "upright (1) or upside down (-1)" },
    { "vel", "x, y", "a velocity" },
    { "velocity", "x, y", "a velocity" },
    { "accel", "x, y", "an acceleration, added to the velocity each tick" },
    { "scale", "x, y", "a scale factor" },
    { "sprpriority", "n", "drawing priority (higher is in front)" },
    { "ontop", "0/1", "drawn on top of everything" },
    { "removetime", "ticks", "removed after this many ticks (-1: never, -2: when its animation ends)" },
    { "bindtime", "ticks", "stays bound to the player for this many ticks (-1: forever)" },
    { "supermovetime", "ticks", "keeps moving during a SuperPause for this many ticks" },
    { "pausemovetime", "ticks", "keeps moving during a Pause for this many ticks" },
    { "ownpal", "0/1", "its own copy of the palette (not changed by the player's PalFX)" },
    { "removeongethit", "0/1", "removed when the player is hit" },
    { "ignorehitpause", "0/1", "runs during hit pause too" },
    { "persistent", "n", "0: once per state; 1: every tick its triggers are true; n: once every n ticks" },
    { "triggerall", "expression", "must be true for any of the triggers to count" },
    { "trigger1", "expression", "the controller runs when every triggerN line of the lowest true N is true" },
    { "type", "name", "the controller (in a [State] block), or S/C/A/L (in a statedef)" },
    { "redirectid", "playerid", "runs the controller on the player with this ID (Ikemen GO)" },
    { "trans", "default/none/add/addalpha/add1/sub", "the transparency" },
    { "alpha", "src, dst", "the transparency's amounts (0-256)" },
    { "channel", "n", "the sound channel (-1: any free one)" },
    { "attr", "SCA, NA/SA/HA/NT/ST/HT/NP/SP/HP", "the attack's attributes: standing, crouching, air; normal, special, hyper; attack, throw, projectile" },
    { "damage", "hit, guard", "damage done when it hits, and when it is guarded" },
    { "animtype", "light/medium/hard/back/up/diagup", "how the opponent reacts to the hit" },
    { "guardflag", "HLA", "how it can be guarded: high, low, in the air (M = HL)" },
    { "hitflag", "HLAFD+-", "what it can hit: high, low, air, falling (F), lying down (D); + only hit states, - not them" },
    { "pausetime", "p1, p2", "hit pause of the attacker and shake time of the target" },
    { "guard.pausetime", "p1, p2", "the same when guarded" },
    { "sparkno", "[S]n", "the hit spark's action (S prefix: the character's own)" },
    { "guard.sparkno", "[S]n", "the guard spark's action" },
    { "sparkxy", "x, y", "where the spark goes, from the target's front and the attacker's axis" },
    { "hitsound", "[S]group, n", "the sound of the hit (S prefix: the character's own)" },
    { "guardsound", "[S]group, n", "the sound when guarded" },
    { "ground.type", "high/low/trip/none", "the kind of hit on a standing or crouching target" },
    { "air.type", "high/low/trip/none", "the kind of hit on a target in the air" },
    { "ground.slidetime", "ticks", "how long the target slides back" },
    { "ground.hittime", "ticks", "how long the target stays in its hit state" },
    { "air.hittime", "ticks", "the same for a target in the air" },
    { "guard.slidetime", "ticks", "how long a guarding target slides back" },
    { "guard.hittime", "ticks", "how long a guarding target stays in its guard state" },
    { "guard.ctrltime", "ticks", "when a guarding target gets control back" },
    { "ground.velocity", "x, y", "the velocity given to a target on the ground" },
    { "air.velocity", "x, y", "the velocity given to a target in the air" },
    { "guard.velocity", "x", "the velocity given to a guarding target" },
    { "airguard.velocity", "x, y", "the velocity given to a target guarding in the air" },
    { "fall", "0/1", "the target falls down" },
    { "air.fall", "0/1", "a target in the air falls down" },
    { "fall.recover", "0/1", "the target can recover from the fall" },
    { "fall.damage", "n", "damage when the target hits the ground" },
    { "fall.xvelocity", "x", "x velocity of the target when it bounces off the ground" },
    { "fall.yvelocity", "y", "y velocity of the target when it bounces off the ground" },
    { "p1stateno", "state", "the attacker goes to this state when it hits" },
    { "p2stateno", "state", "the target goes to this state of the attacker's (a custom state)" },
    { "p2getp1state", "0/1", "with p2stateno, the target uses the attacker's states" },
    { "p1facing", "1/-1", "the attacker turns around (-1) when it hits" },
    { "p2facing", "1/-1/0", "the target faces the attacker (1) or away (-1)" },
    { "p1sprpriority", "n", "the attacker's drawing priority when it hits" },
    { "p2sprpriority", "n", "the target's drawing priority when hit" },
    { "chainid", "n", "this hit's ID, for nochainid" },
    { "nochainid", "n, n", "it cannot hit after a hit with these chainids" },
    { "hitonce", "0/1", "hits one opponent only" },
    { "numhits", "n", "hits added to the combo counter" },
    { "getpower", "hit, guard", "power the attacker gets" },
    { "givepower", "hit, guard", "power the target gets" },
    { "kill", "0/1", "the hit can KO" },
    { "guard.kill", "0/1", "a guarded hit can KO" },
    { "fall.kill", "0/1", "the fall can KO" },
    { "priority", "n, hit/miss/dodge", "which of two hits that meet wins" },
    { "stateno", "state", "the state it starts in" },
    { "helpertype", "normal/player", "a normal helper, or one treated as a player" },
    { "name", "\"text\"", "a name" },
    { "keyctrl", "0/1", "the helper reads the player's keys" },
    { "v", "n", "the int variable's number" },
    { "fv", "n", "the float variable's number" },
    { "sysvar", "n", "the system int variable's number" },
    { "sysfvar", "n", "the system float variable's number" },
    { "first", "n", "the first variable of the range" },
    { "last", "n", "the last variable of the range" },
    { "range", "min, max", "the random number's range" },
    { "flag", "name", "a special flag (see AssertSpecial)" },
    { "movetype", "I/A/H", "idle, attacking, being hit" },
    { "physics", "S/C/A/N", "stand, crouch, air or none: how the engine moves the player" },
    { "juggle", "n", "juggle points the state's attacks need" },
    { "poweradd", "n", "power added when the state starts" },
    { "velset", "x, y", "the velocity set when the state starts" },
    { "facep2", "0/1", "turns toward the opponent when the state starts" },
    { "hitdefpersist", "0/1", "the HitDef stays active in the next state" },
    { "movehitpersist", "0/1", "MoveHit and the like keep their values in the next state" },
    { "hitcountpersist", "0/1", "the hit counter keeps its value in the next state" },
    { "projid", "n", "the projectile's ID" },
    { "projanim", "action", "the projectile's action" },
    { "projhitanim", "action", "its action when it hits" },
    { "projremanim", "action", "its action when it is removed" },
    { "projcancelanim", "action", "its action when it is cancelled by another projectile" },
    { "projhits", "n", "how many hits before it is removed" },
    { "projpriority", "n", "against other projectiles: the higher one survives" },
    { "offset", "x, y", "an offset from where it would be" },
    { "under", "0/1", "drawn behind the players" },
    { "random", "x, y", "a random offset added to pos" },
    { "shadow", "r, g, b", "the shadow's colour (-1: the stage's)" },
    { "color", "0-256", "the colour's saturation (0: grey)" },
    { "add", "r, g, b", "added to the colours" },
    { "mul", "r, g, b", "the colours multiplied by this / 256" },
    { "sinadd", "r, g, b, period", "added to the colours, back and forth over period ticks" },
    { "invertall", "0/1", "the colours inverted" },
    { "darken", "0/1", "darkens the screen" },
    { "movetime", "ticks", "the player keeps moving during the pause for this many ticks" },
    { "freq", "f", "a frequency" },
    { "ampl", "n", "an amplitude" },
    { "phase", "degrees", "a phase" },
    { "edge", "front, back", "the width added to the player's front and back, against the screen's edges" },
    { "player", "front, back", "the width added to the player's front and back, against the other players" },
};

enum { K_Trigger, K_Redirect, K_Controller, K_Param, K_Keyword };

struct NameInfo {
    int trigger = -1;       /* in kTriggers */
    int redirect = -1;      /* in kRedirects */
    int controller = -1;    /* in kControllers */
    int gen_trigger = -1;   /* in kGenTriggers: a trigger the engine's compiler knows */
    int gen_controller = -1; /* in kGenControllers */
    int param = -1;         /* in kParamDocs */
    bool keyword = false;
    bool zss_keyword = false;
    bool listed = false;    /* in Names.words */
    const char *spelling = nullptr; /* as CNS files write it */
};

struct Word {
    const char *name;       /* the known spelling */
    std::string zss;        /* as ZSS files write it: changeState, animElemTime */
    std::string lower;
    int kind;
};

struct Names {
    std::unordered_map<std::string, NameInfo> map;  /* by lower-case name */
    std::vector<Word> words;                        /* for completion, one of each, sorted */
    std::vector<std::vector<std::string>> ctrl_params, trig_args;  /* of kGenControllers, kGenTriggers */
    std::vector<std::string> statedef_params;
    std::deque<std::string> spellings;              /* the names' spellings that are not in a table */
};

/* The lists read from the engine's compiler (scripts/gen_names) */
struct GenName {
    const char *name;
    const char *words;
};
#include "ikemen_names.inc"

static char ToLower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c;
}

static std::string Lower(const char *s, size_t n)
{
    std::string r(s, n);
    for (char &ch : r) {
        ch = ToLower(ch);
    }
    return r;
}

/* The words of a space separated list. */
static std::vector<std::string> SplitWords(const char *s)
{
    std::vector<std::string> out;
    for (const char *p = s; *p;) {
        const char *e = strchr(p, ' ');
        if (e == nullptr) {
            e = p + strlen(p);
        }
        if (e > p) {
            out.emplace_back(p, e);
        }
        p = *e ? e + 1 : e;
    }
    return out;
}

/* A CNS spelling as ZSS writes it: AnimElemTime -> animElemTime, AILevel -> aiLevel, ID -> id. */
static std::string Camel(const char *name)
{
    std::string s = name;
    size_t run = 0;
    while (run < s.size() && s[run] >= 'A' && s[run] <= 'Z') {
        run++;
    }
    size_t lower = run < s.size() && run > 1 && s[run] >= 'a' && s[run] <= 'z' ? run - 1 : run;
    for (size_t i = 0; i < lower; i++) {
        s[i] = ToLower(s[i]);
    }
    return s;
}

static void AddName(Names &names, const char *name, int kind, int index)
{
    std::string low = Lower(name, strlen(name));
    NameInfo &ni = names.map[low];
    switch (kind) {
    case K_Trigger: ni.trigger = index; break;
    case K_Redirect: ni.redirect = index; break;
    case K_Controller: ni.controller = index; break;
    case K_Keyword: ni.keyword = true; break;
    default: break;
    }
    if (!ni.listed) {
        ni.listed = true;
        ni.spelling = name;
        names.words.push_back(Word{ name, "", low, kind });
    }
}

static const Names &KnownNames()
{
    static Names names;
    if (!names.words.empty()) {
        return names;
    }
    for (int i = 0; i < IM_ARRAYSIZE(kControllers); i++) {
        AddName(names, kControllers[i].name, K_Controller, i);
    }
    for (int i = 0; i < IM_ARRAYSIZE(kTriggers); i++) {
        AddName(names, kTriggers[i].name, K_Trigger, i);
    }
    for (int i = 0; i < IM_ARRAYSIZE(kRedirects); i++) {
        AddName(names, kRedirects[i].name, K_Redirect, i);
    }
    for (int i = 0; i < IM_ARRAYSIZE(kKeywords); i++) {
        AddName(names, kKeywords[i], K_Keyword, i);
    }
    for (int i = 0; i < IM_ARRAYSIZE(kParamDocs); i++) {
        AddName(names, kParamDocs[i].name, K_Param, i);
        names.map[Lower(kParamDocs[i].name, strlen(kParamDocs[i].name))].param = i;
    }
    for (int i = 0; i < IM_ARRAYSIZE(kParams); i++) {
        AddName(names, kParams[i], K_Param, i);
    }
    for (const char *k : kZssKeywords) {
        names.map[k].zss_keyword = true;
    }
    /* What the engine's compiler knows: its spelling in ZSS, and as CNS writes names (AnimElemVar) */
    auto add_gen = [&](const char *gen, int kind, int i) {
        std::string low = Lower(gen, strlen(gen));
        NameInfo &ni = names.map[low];
        (kind == K_Controller ? ni.gen_controller : ni.gen_trigger) = i;
        if (!ni.listed) {
            ni.listed = true;
            std::string pascal = gen;
            pascal[0] = (char)toupper((unsigned char)pascal[0]);
            names.spellings.push_back(pascal);
            ni.spelling = names.spellings.back().c_str();
            names.words.push_back(Word{ ni.spelling, gen, low, kind });
        }
    };
    for (int i = 0; i < IM_ARRAYSIZE(kGenControllers); i++) {
        add_gen(kGenControllers[i].name, K_Controller, i);
        names.ctrl_params.push_back(SplitWords(kGenControllers[i].words));
    }
    for (int i = 0; i < IM_ARRAYSIZE(kGenTriggers); i++) {
        add_gen(kGenTriggers[i].name, K_Trigger, i);
        names.trig_args.push_back(SplitWords(kGenTriggers[i].words));
    }
    names.statedef_params = SplitWords(kGenStatedefParams);
    for (Word &w : names.words) {
        if (!w.zss.empty()) {
            continue;
        }
        const NameInfo &ni = names.map[w.lower];
        if (w.kind == K_Controller && ni.gen_controller >= 0) {
            w.zss = kGenControllers[ni.gen_controller].name;
        } else if ((w.kind == K_Trigger || w.kind == K_Redirect) && ni.gen_trigger >= 0) {
            w.zss = kGenTriggers[ni.gen_trigger].name;
        } else if (w.kind == K_Param || w.kind == K_Keyword) {
            w.zss = w.name;
        } else {
            w.zss = Camel(w.name);
        }
    }
    std::sort(names.words.begin(), names.words.end(), [](const Word &a, const Word &b) { return a.lower < b.lower; });
    return names;
}

static const NameInfo *FindName(const char *s, size_t n)
{
    if (n == 0 || n > 64) {
        return nullptr;
    }
    const Names &names = KnownNames();
    auto it = names.map.find(Lower(s, n));
    return it == names.map.end() ? nullptr : &it->second;
}

static bool HasDoc(const NameInfo &ni)
{
    return ni.trigger >= 0 || ni.redirect >= 0 || ni.controller >= 0 || ni.gen_trigger >= 0 || ni.gen_controller >= 0;
}

/* ------------------------------------------------------------------ colours */

enum {
    C_Text, C_Comment, C_Header, C_Key, C_Op, C_Number, C_String, C_Trigger, C_Redirect, C_Controller, C_Keyword,
    C_Local, C_Count
};

struct Palette {
    ImU32 syntax[C_Count];
    ImU32 background, gutter_bg, line_number, line_number_cur, current_line, selection, error_bg, error_mark, cursor;
};

static const Palette kDark = {
    {
        IM_COL32(212, 212, 212, 255),   /* text */
        IM_COL32(106, 153, 85, 255),    /* ; comments */
        IM_COL32(230, 180, 90, 255),    /* [group headers] */
        IM_COL32(156, 220, 254, 255),   /* key = */
        IM_COL32(160, 160, 172, 255),   /* = and operators */
        IM_COL32(181, 206, 168, 255),   /* numbers */
        IM_COL32(206, 145, 120, 255),   /* "strings" */
        IM_COL32(220, 220, 150, 255),   /* triggers */
        IM_COL32(197, 134, 192, 255),   /* redirections */
        IM_COL32(78, 201, 176, 255),    /* controller types */
        IM_COL32(197, 134, 192, 255),   /* loopstart, ZSS's if, for... */
        IM_COL32(255, 196, 120, 255),   /* ZSS's $locals */
    },
    IM_COL32(28, 28, 32, 255),          /* background */
    IM_COL32(34, 34, 40, 255),          /* gutter */
    IM_COL32(110, 110, 125, 255),       /* line numbers */
    IM_COL32(200, 200, 210, 255),       /* the cursor's line number */
    IM_COL32(255, 255, 255, 14),        /* the cursor's line */
    IM_COL32(38, 79, 120, 255),         /* selection */
    IM_COL32(150, 30, 30, 110),         /* the error's line */
    IM_COL32(230, 60, 60, 255),         /* the error's mark */
    IM_COL32(240, 240, 240, 255),       /* cursor */
};

static const Palette kLight = {
    {
        IM_COL32(30, 30, 30, 255),
        IM_COL32(0, 128, 0, 255),
        IM_COL32(170, 85, 0, 255),
        IM_COL32(0, 60, 150, 255),
        IM_COL32(95, 95, 105, 255),
        IM_COL32(9, 134, 88, 255),
        IM_COL32(163, 21, 21, 255),
        IM_COL32(121, 94, 38, 255),
        IM_COL32(150, 0, 190, 255),
        IM_COL32(20, 120, 145, 255),
        IM_COL32(150, 0, 190, 255),
        IM_COL32(0, 110, 110, 255),
    },
    IM_COL32(255, 255, 255, 255),
    IM_COL32(242, 242, 238, 255),
    IM_COL32(150, 150, 150, 255),
    IM_COL32(40, 40, 40, 255),
    IM_COL32(0, 0, 0, 12),
    IM_COL32(173, 214, 255, 255),
    IM_COL32(255, 80, 80, 60),
    IM_COL32(205, 40, 40, 255),
    IM_COL32(20, 20, 20, 255),
};

static const Palette *pal = &kDark;

void Editor_SetLight(bool light)
{
    pal = light ? &kLight : &kDark;
}

static float zoom = 1.0f;  /* the text size of every editor, a factor of the font's */

void Editor_SetZoom(float z)
{
    zoom = std::max(0.5f, std::min(3.0f, z));
}

float Editor_Zoom()
{
    return zoom;
}

static bool IsBlank(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static bool IsDigit(char c)
{
    return c >= '0' && c <= '9';
}

static bool IsAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/* A character of a name: letters, digits, '_' and '.' (ground.velocity). */
static bool IsIdent(char c)
{
    return IsAlpha(c) || IsDigit(c) || c == '_' || c == '.';
}

/* For moving by words: 0 blanks, 1 words (bytes >= 0x80 too), 2 the rest. */
static int CharClass(char c)
{
    if (IsBlank(c)) {
        return 0;
    }
    return IsIdent(c) || (unsigned char)c >= 0x80 ? 1 : 2;
}

static bool EqualsNoCase(const char *s, size_t n, const char *word)
{
    size_t len = strlen(word);
    if (n != len) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (ToLower(s[i]) != word[i]) {
            return false;
        }
    }
    return true;
}

/* Colours an expression (or the values of an AIR line): names, numbers, strings, operators. */
static void ColourValues(const char *s, size_t b, size_t e, unsigned char *out, bool controller)
{
    size_t p = b;
    while (p < e) {
        char c = s[p];
        if (c == '"') {
            size_t q = p + 1;
            while (q < e && s[q] != '"') {
                q++;
            }
            if (q < e) {
                q++;
            }
            memset(out + p, C_String, q - p);
            p = q;
        } else if (IsDigit(c) || (c == '.' && p + 1 < e && IsDigit(s[p + 1]))) {
            size_t q = p;
            while (q < e && (IsDigit(s[q]) || s[q] == '.')) {
                q++;
            }
            memset(out + p, C_Number, q - p);
            p = q;
        } else if (IsAlpha(c) || c == '_') {
            size_t q = p;
            while (q < e && IsIdent(s[q])) {
                q++;
            }
            int col = C_Text;
            const NameInfo *ni = FindName(s + p, q - p);
            if (ni != nullptr) {
                if (controller) {
                    col = ni->controller >= 0 || ni->gen_controller >= 0 ? C_Controller : C_Text;
                } else if (ni->trigger >= 0) {
                    col = C_Trigger;
                } else if (ni->redirect >= 0) {
                    col = C_Redirect;
                } else if (ni->gen_trigger >= 0) {
                    col = C_Trigger;
                } else if (ni->keyword) {
                    col = C_Keyword;
                }
            }
            memset(out + p, col, q - p);
            p = q;
        } else {
            if (c != '\0' && strchr("+-*/%&|^!<>=~:,()[]", c) != nullptr) {
                out[p] = C_Op;
            }
            p++;
        }
    }
}

/* The end of the code of a line: its first ';' outside "double quotes" (n: none). */
static size_t CommentStart(const char *s, size_t n)
{
    bool quoted = false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '"') {
            quoted = !quoted;
        } else if (s[i] == ';' && !quoted) {
            return i;
        }
    }
    return n;
}

/* The '=' of a "key = value" line (before end, outside quotes), n: none. */
static size_t FindEquals(const char *s, size_t from, size_t end)
{
    bool quoted = false;
    for (size_t i = from; i < end; i++) {
        if (s[i] == '"') {
            quoted = !quoted;
        } else if (s[i] == '=' && !quoted) {
            return i;
        }
    }
    return end;
}

/* Whether the key of the line (before its '=') is "type": its value is a controller name. */
static bool IsTypeKey(const char *s, size_t b, size_t eq)
{
    while (b < eq && IsBlank(s[b])) {
        b++;
    }
    while (eq > b && IsBlank(s[eq - 1])) {
        eq--;
    }
    return EqualsNoCase(s + b, eq - b, "type");
}

/* ZSS: where the code of a line ends, its first '#' outside "double quotes" (n: none). */
static size_t ZssCommentStart(const char *s, size_t n)
{
    bool quoted = false;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '"') {
            quoted = !quoted;
        } else if (s[i] == '#' && !quoted) {
            return i;
        }
    }
    return n;
}

static bool IsController(const NameInfo *ni)
{
    return ni != nullptr && (ni->controller >= 0 || ni->gen_controller >= 0);
}

/* ZSS's statements, b to e: controller{key: value; ...}, if/for/switch, $locals, let, :=. */
static void ColourZss(const char *s, size_t b, size_t e, unsigned char *out)
{
    size_t p = b;
    while (p < e) {
        char c = s[p];
        if (c == '"') {
            size_t q = p + 1;
            while (q < e && s[q] != '"') {
                q++;
            }
            q = std::min(q + 1, e);
            memset(out + p, C_String, q - p);
            p = q;
        } else if (c == '$' && p + 1 < e && (IsAlpha(s[p + 1]) || s[p + 1] == '_')) {
            size_t q = p + 1;
            while (q < e && IsIdent(s[q])) {
                q++;
            }
            memset(out + p, C_Local, q - p);
            p = q;
        } else if (IsDigit(c) || (c == '.' && p + 1 < e && IsDigit(s[p + 1]))) {
            size_t q = p;
            while (q < e && (IsDigit(s[q]) || s[q] == '.')) {
                q++;
            }
            memset(out + p, C_Number, q - p);
            p = q;
        } else if (IsAlpha(c) || c == '_') {
            size_t q = p;
            while (q < e && IsIdent(s[q])) {
                q++;
            }
            size_t r = q;
            while (r < e && IsBlank(s[r])) {
                r++;
            }
            const NameInfo *ni = FindName(s + p, q - p);
            int col = C_Text;
            if (r < e && s[r] == ':' && (r + 1 >= e || s[r + 1] != '=')) {
                col = C_Key;    /* a parameter: value: 200 */
            } else if (r < e && s[r] == '{' && IsController(ni)) {
                col = C_Controller;
            } else if (ni != nullptr && ni->zss_keyword) {
                col = C_Keyword;
            } else if (ni != nullptr && ni->trigger >= 0) {
                col = C_Trigger;
            } else if (ni != nullptr && ni->redirect >= 0) {
                col = C_Redirect;
            } else if (ni != nullptr && ni->gen_trigger >= 0) {
                col = C_Trigger;
            }
            memset(out + p, col, q - p);
            p = q;
        } else {
            if (c != '\0' && strchr("+-*/%&|^!<>=~:,()[]{};", c) != nullptr) {
                out[p] = C_Op;
            }
            p++;
        }
    }
}

/* A ZSS line: # comments; [StateDef n; key: value; ...] and [Function name(args)] headers. */
static void HighlightZss(const char *s, size_t n, unsigned char *out)
{
    size_t end = ZssCommentStart(s, n);
    memset(out + end, C_Comment, n - end);
    size_t i = 0;
    while (i < end && IsBlank(s[i])) {
        i++;
    }
    if (i < end && s[i] == '[') {
        /* its name and number up to the first ';', then key: value pairs */
        size_t j = i;
        while (j < end && s[j] != ';' && s[j] != ']') {
            j++;
        }
        memset(out + i, C_Header, j - i);
        size_t close = j;
        while (close < end && s[close] != ']') {
            close++;
        }
        if (j < close) {
            ColourZss(s, j, close, out);
        }
        if (close < end) {
            out[close] = C_Header;
        }
        return;
    }
    ColourZss(s, i, end, out);
}

/* The colour of each byte of a line. */
static void Highlight(const char *s, size_t n, std::vector<unsigned char> &colors, bool zss = false)
{
    colors.assign(n + 1, (unsigned char)C_Text);
    if (zss) {
        HighlightZss(s, n, colors.data());
        return;
    }
    unsigned char *out = colors.data();
    size_t end = CommentStart(s, n);
    memset(out + end, C_Comment, n - end);
    size_t i = 0;
    while (i < end && IsBlank(s[i])) {
        i++;
    }
    if (i == end) {
        return;
    }
    if (s[i] == '[') {
        size_t j = i;
        while (j < end && s[j] != ']') {
            j++;
        }
        if (j < end) {
            j++;
        }
        memset(out + i, C_Header, j - i);
        return;
    }
    size_t eq = FindEquals(s, i, end);
    if (eq < end) {
        size_t ke = eq;
        while (ke > i && IsBlank(s[ke - 1])) {
            ke--;
        }
        memset(out + i, C_Key, ke - i);
        out[eq] = C_Op;
        ColourValues(s, eq + 1, end, out, IsTypeKey(s, i, eq));
        return;
    }
    /* AIR: "Clsn2: 1", "Clsn1Default: 2" */
    size_t colon = i;
    while (colon < end && IsIdent(s[colon])) {
        colon++;
    }
    if (colon > i && colon < end && s[colon] == ':') {
        memset(out + i, C_Key, colon - i);
        out[colon] = C_Op;
        ColourValues(s, colon + 1, end, out, false);
        return;
    }
    ColourValues(s, i, end, out, false);
}

/* ------------------------------------------------------------------ the editor */

static const size_t kNone = (size_t)-1;
static const size_t kUndoMax = 500;
static const float kMargin = 6.0f;      /* between the gutter and the text */

enum { G_None, G_Type, G_Backspace, G_Delete };

/* One change of the text: what was at `at` and what replaced it. */
struct UndoStep {
    size_t at = 0;
    std::string removed;
    std::string inserted;
    size_t cur_before = 0, anchor_before = 0;
    size_t cur_after = 0, anchor_after = 0;
    int group = G_None;         /* typing, backspaces and deletes in a row merge */
};

/* The documentation of a name, as it is used there. */
enum { D_None, D_Name, D_Param, D_Arg };
struct Doc {
    int what = D_None;
    std::string name;           /* lower case: a trigger or controller (D_Name), a parameter, a name a trigger takes */
    std::string ctrl;           /* D_Param: its controller ("" a statedef's); D_Arg: the trigger */
    bool controller_first = false;  /* D_Name: a name that is a controller and a trigger shows the controller */
};

/* A name the completion offers: what it puts, what follows it (the cursor goes before a '}' of it). */
struct CompItem {
    std::string text;
    std::string tail;
    const char *kind;
    Doc doc;
};

struct CodeEditor {
    /* the caller's text (while drawing), and what was known of it */
    std::string *text = nullptr;
    const std::string *seen = nullptr;
    unsigned int seen_version = 0;
    size_t seen_size = 0;
    std::vector<size_t> lines;  /* where each line starts */
    float max_width = 0.0f;     /* of the longest line, in pixels */
    float measured_size = 0.0f; /* the font size max_width was measured with, 0: measure again */

    size_t cur = 0, anchor = 0; /* the cursor, and the other end of the selection */
    float want_x = -1.0f;       /* the x Up and Down keep, -1: none */
    float blink = 0.0f;

    ImGuiID id = 0;
    bool focused = false;
    bool want_focus = false;
    int scroll = 0;             /* bring the cursor into view: 1 just enough, 2 centred */
    float scroll_dy = 0.0f;     /* scroll by this much (Ctrl+Up/Down, PageUp/PageDown) */
    bool dragging = false;
    bool changed = false;       /* this frame */

    int goto_line = 0;          /* asked for with Editor_GotoLine, done at the next draw */
    bool goto_select = false;
    size_t sel_begin = kNone, sel_end = kNone;  /* asked for with Editor_Select */

    std::vector<UndoStep> undo;
    size_t undo_done = 0;       /* steps of undo that are applied (the rest can be redone) */
    bool merge = false;         /* the next edit may merge with the last step */

    /* completion */
    bool comp_open = false;
    size_t comp_begin = 0;      /* where the word being completed starts */
    std::vector<CompItem> comp_items;
    int comp_sel = 0;
    int comp_top = 0;
    bool comp_moved = false;    /* Up/Down used */
    ImVec2 comp_pos;

    int error_line = 0;
    std::string error_msg;

    /* Editor_SetDecorations: marks left of the line numbers, line backgrounds */
    std::function<int(int)> mark_of;
    std::function<ImU32(int)> background_of;
    std::function<std::string(int, ImU32 *)> note_of;
    int mark_clicked = 0;
    size_t ctrl_clicked = kNone;

    /* Editor_SetHighlights */
    std::vector<std::pair<size_t, size_t>> highlights;
    int highlight_cur = -1;

    bool zss = false;           /* the text is ZSS (Editor_SetZss) */
    std::function<std::string(size_t, size_t)> hover_note;  /* Editor_SetHoverNote */

    size_t hover_word = kNone;  /* the word under the mouse, and for how long */
    float hover_time = 0.0f;
    std::string hover_text;     /* hover_note's for it */

    std::vector<unsigned char> colors;  /* of the line being drawn */
};

/* The font measures of a frame. */
struct Metrics {
    ImFont *font;
    ImFontBaked *baked;
    float size;
    float scale;
    float adv[128];
    float space;
    float tab;
    float line_h;
};

static void InitMetrics(Metrics *m)
{
    m->font = ImGui::GetFont();
    m->size = ImGui::GetFontSize();
    m->baked = ImGui::GetFontBaked();
    m->scale = m->baked->Size > 0.0f ? m->size / m->baked->Size : 1.0f;
    for (int c = 0; c < 128; c++) {
        m->adv[c] = m->baked->GetCharAdvance((ImWchar)c) * m->scale;
    }
    m->adv['\r'] = 0.0f;
    m->adv['\n'] = 0.0f;
    m->space = m->adv[' '] > 0.0f ? m->adv[' '] : m->size * 0.5f;
    m->tab = m->space * 4.0f;
    m->line_h = floorf(m->size + 2.0f);
}

/*
 * The character at s: a valid UTF-8 sequence, else one byte, shown as Latin-1 (old
 * files have Latin-1 or Shift-JIS comments). Returns its length in bytes.
 */
static int CharAt(const char *s, const char *e, unsigned int *out)
{
    unsigned char c = (unsigned char)*s;
    *out = c;
    if (c < 0x80) {
        return 1;
    }
    int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 ? 2 : 0;
    if (len == 0 || c >= 0xF5 || e - s < len) {
        return 1;
    }
    unsigned int v = c & (0x7Fu >> len);
    for (int i = 1; i < len; i++) {
        unsigned char d = (unsigned char)s[i];
        if ((d & 0xC0) != 0x80) {
            return 1;
        }
        v = (v << 6) | (d & 0x3Fu);
    }
    if ((len == 3 && v < 0x800) || (len == 4 && v < 0x10000) || (v >= 0xD800 && v <= 0xDFFF)) {
        return 1;
    }
    *out = v <= (unsigned int)IM_UNICODE_CODEPOINT_MAX ? v : (unsigned int)IM_UNICODE_CODEPOINT_INVALID;
    return len;
}

static float Advance(const Metrics &m, unsigned int c, float x)
{
    if (c < 128) {
        if (c == '\t') {
            return (floorf(x / m.tab) + 1.0f) * m.tab - x;
        }
        return m.adv[c];
    }
    return m.baked->GetCharAdvance((ImWchar)c) * m.scale;
}

/* The x of p in the line [s, e), from the start of the line. */
static float TextX(const Metrics &m, const char *s, const char *e, const char *p)
{
    float x = 0.0f;
    while (s < e && s < p) {
        unsigned int c;
        s += CharAt(s, e, &c);
        x += Advance(m, c, x);
    }
    return x;
}

static int LineCount(const CodeEditor *ed)
{
    return (int)ed->lines.size();
}

static int LineOf(const CodeEditor *ed, size_t pos)
{
    auto it = std::upper_bound(ed->lines.begin(), ed->lines.end(), pos);
    int l = (int)(it - ed->lines.begin()) - 1;
    return l < 0 ? 0 : l;
}

/* Where the text of line l ends (before its '\n', and before a '\r' there). */
static size_t LineEnd(const CodeEditor *ed, const std::string &t, int l)
{
    size_t b = ed->lines[l];
    size_t e = l + 1 < LineCount(ed) ? ed->lines[l + 1] - 1 : t.size();
    if (e > b && t[e - 1] == '\r') {
        e--;
    }
    return e;
}

/* Where line l ends with its '\r', before its '\n'. */
static size_t LineRawEnd(const CodeEditor *ed, const std::string &t, int l)
{
    return l + 1 < LineCount(ed) ? ed->lines[l + 1] - 1 : t.size();
}

static size_t ClampPos(const CodeEditor *ed, const std::string &t, size_t p)
{
    if (p > t.size()) {
        p = t.size();
    }
    return std::min(p, LineEnd(ed, t, LineOf(ed, p)));
}

static void BuildLines(CodeEditor *ed, const std::string &t)
{
    ed->lines.clear();
    ed->lines.push_back(0);
    const char *s = t.data();
    size_t n = t.size();
    const char *p = s;
    while ((p = (const char *)memchr(p, '\n', n - (size_t)(p - s))) != nullptr) {
        p++;
        ed->lines.push_back((size_t)(p - s));
    }
    ed->measured_size = 0.0f;
}

static float XOf(const CodeEditor *ed, const Metrics &m, const std::string &t, size_t pos)
{
    int l = LineOf(ed, pos);
    const char *d = t.data();
    return TextX(m, d + ed->lines[l], d + LineEnd(ed, t, l), d + pos);
}

/* The place in line l nearest to x. */
static size_t PosAtX(const CodeEditor *ed, const Metrics &m, const std::string &t, int l, float x)
{
    const char *d = t.data();
    const char *s = d + ed->lines[l], *e = d + LineEnd(ed, t, l);
    float cx = 0.0f;
    while (s < e) {
        unsigned int c;
        int n = CharAt(s, e, &c);
        float a = Advance(m, c, cx);
        if (x < cx + a * 0.5f) {
            break;
        }
        cx += a;
        s += n;
    }
    return (size_t)(s - d);
}

static size_t NextChar(const CodeEditor *ed, const std::string &t, size_t pos)
{
    int l = LineOf(ed, pos);
    size_t e = LineEnd(ed, t, l);
    if (pos >= e) {
        return l + 1 < LineCount(ed) ? ed->lines[l + 1] : pos;
    }
    unsigned int c;
    return pos + (size_t)CharAt(t.data() + pos, t.data() + e, &c);
}

static size_t PrevChar(const CodeEditor *ed, const std::string &t, size_t pos)
{
    int l = LineOf(ed, pos);
    size_t b = ed->lines[l];
    if (pos <= b) {
        return l > 0 ? LineEnd(ed, t, l - 1) : 0;
    }
    /* back over the continuation bytes of a UTF-8 sequence that ends at pos */
    size_t p = pos - 1;
    for (int k = 0; k < 3 && p > b && ((unsigned char)t[p] & 0xC0) == 0x80; k++) {
        p--;
    }
    unsigned int c;
    if (p + (size_t)CharAt(t.data() + p, t.data() + t.size(), &c) == pos) {
        return p;
    }
    return pos - 1;
}

static size_t WordRight(const CodeEditor *ed, const std::string &t, size_t pos)
{
    int l = LineOf(ed, pos);
    size_t e = LineEnd(ed, t, l);
    if (pos >= e) {
        return NextChar(ed, t, pos);
    }
    int cls = CharClass(t[pos]);
    while (pos < e && CharClass(t[pos]) == cls) {
        pos++;
    }
    while (pos < e && CharClass(t[pos]) == 0) {
        pos++;
    }
    return pos;
}

static size_t WordLeft(const CodeEditor *ed, const std::string &t, size_t pos)
{
    int l = LineOf(ed, pos);
    size_t b = ed->lines[l];
    if (pos <= b) {
        return PrevChar(ed, t, pos);
    }
    while (pos > b && CharClass(t[pos - 1]) == 0) {
        pos--;
    }
    if (pos > b) {
        int cls = CharClass(t[pos - 1]);
        while (pos > b && CharClass(t[pos - 1]) == cls) {
            pos--;
        }
    }
    return pos;
}

/* The first character of line l that is not blank. */
static size_t LineIndentEnd(const CodeEditor *ed, const std::string &t, int l)
{
    size_t p = ed->lines[l], e = LineEnd(ed, t, l);
    while (p < e && (t[p] == ' ' || t[p] == '\t')) {
        p++;
    }
    return p;
}

static bool HasSelection(const CodeEditor *ed)
{
    return ed->cur != ed->anchor;
}

static size_t SelBegin(const CodeEditor *ed)
{
    return std::min(ed->cur, ed->anchor);
}

static size_t SelEnd(const CodeEditor *ed)
{
    return std::max(ed->cur, ed->anchor);
}

/* The lines the selection (or the cursor) touches; a selection ending at column 0 leaves that line out. */
static void SelLines(const CodeEditor *ed, int *a, int *b)
{
    size_t sb = SelBegin(ed), se = SelEnd(ed);
    *a = LineOf(ed, sb);
    *b = LineOf(ed, se);
    if (*b > *a && se == ed->lines[*b]) {
        (*b)--;
    }
}

static void CloseCompletion(CodeEditor *ed)
{
    ed->comp_open = false;
    ed->comp_items.clear();
}

static void MoveTo(CodeEditor *ed, size_t pos, bool shift, bool keep_x = false)
{
    ed->cur = pos;
    if (!shift) {
        ed->anchor = pos;
    }
    if (!keep_x) {
        ed->want_x = -1.0f;
    }
    ed->merge = false;
    ed->scroll = std::max(ed->scroll, 1);
    ed->blink = -0.3f;
    CloseCompletion(ed);
}

static void Changed(CodeEditor *ed)
{
    BuildLines(ed, *ed->text);
    ed->changed = true;
    ed->want_x = -1.0f;
    ed->scroll = std::max(ed->scroll, 1);
    ed->blink = -0.3f;
}

/* After an edit moved the cursor or the selection: what redo brings back. */
static void RecordAfter(CodeEditor *ed)
{
    if (ed->undo_done > 0) {
        ed->undo[ed->undo_done - 1].cur_after = ed->cur;
        ed->undo[ed->undo_done - 1].anchor_after = ed->anchor;
    }
}

/* Replaces [begin, end) with str, as one step of undo (or merged into the last one); the cursor goes after str. */
static void Replace(CodeEditor *ed, size_t begin, size_t end, const std::string &str, int group)
{
    std::string &t = *ed->text;
    if (begin == end && str.empty()) {
        return;
    }
    bool merged = false;
    if (ed->merge && group != G_None && ed->undo_done == ed->undo.size() && !ed->undo.empty()) {
        UndoStep &last = ed->undo.back();
        if (last.group == group) {
            if (group == G_Type && begin == end && begin == last.at + last.inserted.size()) {
                /* a new word starts a new step */
                bool space = str[0] == ' ' || str[0] == '\t';
                bool last_space = !last.inserted.empty() && (last.inserted.back() == ' ' || last.inserted.back() == '\t');
                if (!space || last_space) {
                    last.inserted += str;
                    merged = true;
                }
            } else if (group == G_Backspace && str.empty() && last.inserted.empty() && end == last.at) {
                last.removed.insert(0, t, begin, end - begin);
                last.at = begin;
                merged = true;
            } else if (group == G_Delete && str.empty() && last.inserted.empty() && begin == last.at) {
                last.removed.append(t, begin, end - begin);
                merged = true;
            }
        }
    }
    if (!merged) {
        ed->undo.resize(ed->undo_done);
        UndoStep st;
        st.at = begin;
        st.removed = t.substr(begin, end - begin);
        st.inserted = str;
        st.cur_before = ed->cur;
        st.anchor_before = ed->anchor;
        st.group = group;
        ed->undo.push_back(std::move(st));
        if (ed->undo.size() > kUndoMax) {
            ed->undo.erase(ed->undo.begin());
        }
        ed->undo_done = ed->undo.size();
    }
    t.replace(begin, end - begin, str);
    ed->cur = ed->anchor = begin + str.size();
    ed->merge = group != G_None;
    RecordAfter(ed);
    Changed(ed);
}

static void InsertText(CodeEditor *ed, const std::string &str, int group)
{
    Replace(ed, SelBegin(ed), SelEnd(ed), str, group);
}

static void Undo(CodeEditor *ed)
{
    if (ed->undo_done == 0) {
        return;
    }
    const UndoStep &st = ed->undo[--ed->undo_done];
    ed->text->replace(st.at, st.inserted.size(), st.removed);
    ed->cur = st.cur_before;
    ed->anchor = st.anchor_before;
    ed->merge = false;
    Changed(ed);
    ed->cur = ClampPos(ed, *ed->text, ed->cur);
    ed->anchor = ClampPos(ed, *ed->text, ed->anchor);
}

static void Redo(CodeEditor *ed)
{
    if (ed->undo_done >= ed->undo.size()) {
        return;
    }
    const UndoStep &st = ed->undo[ed->undo_done++];
    ed->text->replace(st.at, st.removed.size(), st.inserted);
    ed->cur = st.cur_after;
    ed->anchor = st.anchor_after;
    ed->merge = false;
    Changed(ed);
    ed->cur = ClampPos(ed, *ed->text, ed->cur);
    ed->anchor = ClampPos(ed, *ed->text, ed->anchor);
}

/* What one level of indentation is in this file: a tab where the lines are indented with tabs, else 2 spaces. */
static std::string IndentUnit(const CodeEditor *ed, const std::string &t, int l)
{
    for (int k = l; k >= 0 && k > l - 200; k--) {
        size_t b = ed->lines[k];
        if (b < t.size() && t[b] == '\t') {
            return "\t";
        }
        if (b < t.size() && t[b] == ' ') {
            return "  ";
        }
    }
    return "  ";
}

/* Indents (or outdents) the lines a..b, as one step of undo. */
static void IndentLines(CodeEditor *ed, int a, int b, bool outdent)
{
    std::string &t = *ed->text;
    std::string unit = IndentUnit(ed, t, a);
    size_t begin = ed->lines[a], end = LineRawEnd(ed, t, b);
    std::string out;
    int cur_line = LineOf(ed, ed->cur);
    size_t cur_col = ed->cur - ed->lines[cur_line];
    long delta_cur = 0;
    for (int l = a; l <= b; l++) {
        size_t lb = ed->lines[l], le = LineRawEnd(ed, t, l);
        long delta = 0;
        if (outdent) {
            size_t k = lb;
            if (k < le && t[k] == '\t') {
                k++;
            } else {
                while (k < le && k < lb + 2 && t[k] == ' ') {
                    k++;
                }
            }
            delta = -(long)(k - lb);
            out.append(t, k, le - k);
        } else {
            if (le > lb) {
                out += unit;
                delta = (long)unit.size();
            }
            out.append(t, lb, le - lb);
        }
        if (l == cur_line) {
            delta_cur = delta;
        }
        if (l < b) {
            out += '\n';
        }
    }
    bool sel = HasSelection(ed);
    Replace(ed, begin, end, out, G_None);
    if (sel) {
        ed->anchor = ed->lines[a];
        ed->cur = LineEnd(ed, t, b);
    } else {
        long col = std::max(0L, (long)cur_col + delta_cur);
        ed->cur = ed->anchor = std::min(ed->lines[cur_line] + (size_t)col, LineEnd(ed, t, cur_line));
    }
    RecordAfter(ed);
}

/* Ctrl+/: comments the lines (';' at column 0), or uncomments them when they all are. */
static void ToggleComment(CodeEditor *ed)
{
    std::string &t = *ed->text;
    const char mark = ed->zss ? '#' : ';';
    int a, b;
    SelLines(ed, &a, &b);
    bool all = true, any = false;
    for (int l = a; l <= b; l++) {
        size_t p = LineIndentEnd(ed, t, l);
        if (p < LineEnd(ed, t, l)) {
            any = true;
            if (t[p] != mark) {
                all = false;
            }
        }
    }
    bool remove = any && all;
    int cur_line = LineOf(ed, ed->cur);
    size_t cur_col = ed->cur - ed->lines[cur_line];
    long delta_cur = 0;
    std::string out;
    for (int l = a; l <= b; l++) {
        size_t lb = ed->lines[l], le = LineRawEnd(ed, t, l);
        size_t ind = LineIndentEnd(ed, t, l);
        long delta = 0;
        if (remove) {
            if (ind < le && t[ind] == mark) {
                out.append(t, lb, ind - lb);
                out.append(t, ind + 1, le - ind - 1);
                delta = cur_col > ind - lb ? -1 : 0;
            } else {
                out.append(t, lb, le - lb);
            }
        } else {
            if (ind < LineEnd(ed, t, l) || a == b) {
                out += mark;
                delta = 1;
            }
            out.append(t, lb, le - lb);
        }
        if (l == cur_line) {
            delta_cur = delta;
        }
        if (l < b) {
            out += '\n';
        }
    }
    bool sel = HasSelection(ed);
    Replace(ed, ed->lines[a], LineRawEnd(ed, t, b), out, G_None);
    if (sel) {
        ed->anchor = ed->lines[a];
        ed->cur = LineEnd(ed, t, b);
    } else {
        long col = std::max(0L, (long)cur_col + delta_cur);
        ed->cur = ed->anchor = std::min(ed->lines[cur_line] + (size_t)col, LineEnd(ed, t, cur_line));
    }
    RecordAfter(ed);
}

/* Ctrl+D: a copy of the lines below them; the cursor goes into the copy. */
static void DuplicateLines(CodeEditor *ed)
{
    std::string &t = *ed->text;
    int a, b;
    SelLines(ed, &a, &b);
    size_t begin = ed->lines[a], end = LineRawEnd(ed, t, b);
    std::string block = "\n" + t.substr(begin, end - begin);
    size_t cur = ed->cur, anchor = ed->anchor;
    Replace(ed, end, end, block, G_None);
    ed->cur = cur + block.size();
    ed->anchor = anchor + block.size();
    RecordAfter(ed);
}

/* Ctrl+Shift+K: removes the lines. */
static void DeleteLines(CodeEditor *ed, const Metrics &m)
{
    std::string &t = *ed->text;
    int a, b;
    SelLines(ed, &a, &b);
    float x = XOf(ed, m, t, ed->cur);
    size_t begin = ed->lines[a];
    size_t end = b + 1 < LineCount(ed) ? ed->lines[b + 1] : t.size();
    if (b + 1 >= LineCount(ed) && a > 0) {
        begin--;    /* the last line: take the '\n' before it */
    }
    Replace(ed, begin, end, std::string(), G_None);
    int l = std::min(a, LineCount(ed) - 1);
    ed->cur = ed->anchor = PosAtX(ed, m, t, l, x);
    RecordAfter(ed);
}

static void Copy(CodeEditor *ed)
{
    if (HasSelection(ed)) {
        std::string s = ed->text->substr(SelBegin(ed), SelEnd(ed) - SelBegin(ed));
        ImGui::SetClipboardText(s.c_str());
    }
}

static void Paste(CodeEditor *ed)
{
    const char *clip = ImGui::GetClipboardText();
    if (clip == nullptr || *clip == '\0') {
        return;
    }
    std::string s;
    for (const char *p = clip; *p != '\0'; p++) {
        if (*p == '\r' && p[1] == '\n') {
            continue;   /* the text has '\n' line ends */
        }
        s += *p;
    }
    InsertText(ed, s, G_None);
}

static void SelectWord(CodeEditor *ed, size_t pos)
{
    const std::string &t = *ed->text;
    int l = LineOf(ed, pos);
    size_t b = ed->lines[l], e = LineEnd(ed, t, l);
    size_t s = pos, f = pos;
    if (pos < e) {
        int cls = CharClass(t[pos]);
        while (s > b && CharClass(t[s - 1]) == cls) {
            s--;
        }
        while (f < e && CharClass(t[f]) == cls) {
            f++;
        }
    } else if (pos > b) {
        int cls = CharClass(t[pos - 1]);
        while (s > b && CharClass(t[s - 1]) == cls) {
            s--;
        }
    }
    ed->anchor = s;
    ed->cur = f;
}

/* ------------------------------------------------------------------ what a place of the text is */

enum { X_None, X_Any, X_Key, X_Controller, X_Statement, X_Expr, X_Arg };

/*
 * What fits at a place of a state file: X_Key a parameter's name (of the controller ctrl, lower
 * case, "" not known yet; of the statedef with statedef), X_Controller a controller's name (CNS's
 * type =), X_Statement a ZSS statement (a controller or a keyword), X_Expr an expression, X_Arg a
 * name the trigger trig takes in its parentheses (GetHitVar(fall.damage)); X_Any outside the
 * statedefs (CMD, AIR, DEF files: every name), X_None nothing (comments, headers, a statedef's type).
 */
struct Context {
    int kind = X_None;
    std::string ctrl;
    bool statedef = false;
    int trig = -1;
};

static std::string TrimLower(const char *s, size_t b, size_t e)
{
    while (b < e && IsBlank(s[b])) {
        b++;
    }
    while (e > b && IsBlank(s[e - 1])) {
        e--;
    }
    return Lower(s + b, e - b);
}

/* The trigger whose parentheses p is just inside ("GetHitVar(" before it), in kGenTriggers; -1 none. */
static int ArgTrigger(const char *s, size_t lb, size_t p)
{
    while (p > lb && IsBlank(s[p - 1])) {
        p--;
    }
    if (p == lb || s[p - 1] != '(') {
        return -1;
    }
    p--;
    while (p > lb && IsBlank(s[p - 1])) {
        p--;
    }
    size_t q = p;
    while (q > lb && IsIdent(s[q - 1])) {
        q--;
    }
    const NameInfo *ni = FindName(s + q, p - q);
    if (ni == nullptr || ni->gen_trigger < 0 || ni->redirect >= 0) {
        return -1;
    }
    return KnownNames().trig_args[ni->gen_trigger].empty() ? -1 : ni->gen_trigger;
}

static void ExprContext(Context &x, const char *s, size_t lb, size_t wb)
{
    x.trig = ArgTrigger(s, lb, wb);
    x.kind = x.trig >= 0 ? X_Arg : X_Expr;
}

/* CNS: [Statedef n] key = value; [State n] key = value, its controller named by its type = line. */
static Context CnsContext(const CodeEditor *ed, const std::string &t, size_t wb)
{
    Context x;
    const char *d = t.data();
    int l = LineOf(ed, wb);
    size_t lb = ed->lines[l], le = LineEnd(ed, t, l);
    size_t end = lb + CommentStart(d + lb, le - lb);
    size_t i = LineIndentEnd(ed, t, l);
    if (wb > end || (i < end && d[i] == '[')) {
        return x;
    }
    /* the section: the header above */
    std::string header;
    int hl = -1;
    for (int k = l; k >= 0 && hl < 0; k--) {
        size_t kb = LineIndentEnd(ed, t, k), ke = LineEnd(ed, t, k);
        if (kb < ke && d[kb] == '[') {
            size_t close = kb;
            while (close < ke && d[close] != ']') {
                close++;
            }
            header = TrimLower(d, kb + 1, close);
            hl = k;
        }
    }
    bool statedef = header.compare(0, 8, "statedef") == 0;
    bool state = !statedef && header.compare(0, 5, "state") == 0;
    if (!state && !statedef) {
        x.kind = X_Any;
        return x;
    }
    size_t eq = FindEquals(d, i, end);
    if (eq >= wb) {
        x.kind = X_Key;
        x.statedef = statedef;
        /* the block's type = line */
        for (int k = hl + 1; state && k < LineCount(ed); k++) {
            size_t kb = LineIndentEnd(ed, t, k), ke = LineEnd(ed, t, k);
            if (kb < ke && d[kb] == '[') {
                break;
            }
            size_t kend = kb + CommentStart(d + kb, ke - kb), keq = FindEquals(d, kb, kend);
            if (keq < kend && IsTypeKey(d, kb, keq)) {
                x.ctrl = TrimLower(d, keq + 1, kend);
                break;
            }
        }
        return x;
    }
    std::string key = TrimLower(d, i, eq);
    if (state && key == "type") {
        x.kind = X_Controller;
    } else if (!(statedef && (key == "type" || key == "movetype" || key == "physics"))) {
        ExprContext(x, d, lb, wb);
    }
    return x;
}

/* ZSS: [StateDef n; key: value; ...]; statements; controller{key: value; ...}, over several lines too. */
static Context ZssContext(const CodeEditor *ed, const std::string &t, size_t wb)
{
    Context x;
    const char *d = t.data();
    int l = LineOf(ed, wb);
    size_t lb = ed->lines[l], le = LineEnd(ed, t, l);
    size_t end = lb + ZssCommentStart(d + lb, le - lb);
    size_t i = LineIndentEnd(ed, t, l);
    if (wb > end) {
        return x;
    }
    if (i < end && d[i] == '[') {
        if (TrimLower(d, i + 1, std::min(i + 9, end)) != "statedef") {
            return x;
        }
        size_t sep = wb;
        while (sep > i && d[sep - 1] != ';') {
            sep--;
        }
        if (sep == i) {
            return x;   /* its number */
        }
        size_t colon = sep;
        while (colon < wb && d[colon] != ':') {
            colon++;
        }
        if (colon >= wb) {
            x.kind = X_Key;
            x.statedef = true;
            return x;
        }
        std::string key = TrimLower(d, sep, colon);
        if (key != "type" && key != "movetype" && key != "physics") {
            ExprContext(x, d, lb, wb);
        }
        return x;
    }
    /* The '{' it is in, back through the lines of the state; at its level, the nearest ';' or
       '{' before the word and whether a ':' (not :=) comes after it */
    int depth = 0;
    bool sep = false, colon = false;
    size_t brace = kNone;
    for (int k = l; k >= 0 && k > l - 400 && brace == kNone; k--) {
        size_t kb = ed->lines[k], ke = LineEnd(ed, t, k);
        size_t ki = LineIndentEnd(ed, t, k);
        if (k < l && ki < ke && d[ki] == '[') {
            break;
        }
        size_t from = k == l ? wb : kb + ZssCommentStart(d + kb, ke - kb);
        for (size_t p = from; p > kb; p--) {
            char c = d[p - 1];
            if (c == '}') {
                depth++;
            } else if (c == '{') {
                if (depth == 0) {
                    brace = p - 1;
                    break;
                }
                depth--;
            } else if (depth == 0 && !sep) {
                if (c == ';') {
                    sep = true;
                } else if (c == ':' && d[p] != '=') {
                    colon = true;
                }
            }
        }
    }
    if (brace != kNone) {
        int bl = LineOf(ed, brace);
        size_t blb = ed->lines[bl], p = brace;
        while (p > blb && IsBlank(d[p - 1])) {
            p--;
        }
        size_t q = p;
        while (q > blb && IsIdent(d[q - 1])) {
            q--;
        }
        if (IsController(FindName(d + q, p - q))) {
            if (!colon) {
                x.kind = X_Key;
                x.ctrl = Lower(d + q, p - q);
            } else {
                ExprContext(x, d, lb, wb);
            }
            return x;
        }
    }
    /* a statement: the first word of its line, or after '{', '}' or ';' */
    size_t p = wb;
    while (p > lb && IsBlank(d[p - 1])) {
        p--;
    }
    if (p == lb || strchr("{};", d[p - 1]) != nullptr) {
        x.kind = X_Statement;
        return x;
    }
    ExprContext(x, d, lb, wb);
    return x;
}

static Context ContextAt(const CodeEditor *ed, const std::string &t, size_t wb)
{
    return ed->zss ? ZssContext(ed, t, wb) : CnsContext(ed, t, wb);
}

/* ------------------------------------------------------------------ documentation */

static ImVec4 Colour(int c)
{
    return ImGui::ColorConvertU32ToFloat4(pal->syntax[c]);
}

static const NameInfo *Info(const std::string &lower)
{
    const Names &names = KnownNames();
    auto it = names.map.find(lower);
    return it == names.map.end() ? nullptr : &it->second;
}

/* A list of names, at most max of them */
static void ShowList(const char *label, const std::vector<std::string> &list, size_t max)
{
    std::string s = label;
    for (size_t i = 0; i < list.size() && i < max; i++) {
        s += (i ? ", " : " ") + list[i];
    }
    if (list.size() > max) {
        s += ", and " + std::to_string(list.size() - max) + " more";
    }
    ImGui::TextDisabled("%s", s.c_str());
}

/* The part of a controller's line in kControllers about a parameter ("value = action"), "" none. */
static std::string ParamPart(const char *syntax, const std::string &param)
{
    std::string s = syntax;
    for (size_t at = 0; at < s.size();) {
        size_t semi = s.find(';', at);
        if (semi == std::string::npos) {
            semi = s.size();
        }
        std::string part = s.substr(at, semi - at);
        part.erase(0, part.find_first_not_of(' '));
        size_t n = 0;
        while (n < part.size() && IsIdent(part[n])) {
            n++;
        }
        if (Lower(part.data(), n) == param) {
            return part;
        }
        at = semi + 1;
    }
    return "";
}

static void ShowDoc(const Doc &doc)
{
    const Names &names = KnownNames();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
    const NameInfo *ni = Info(doc.name);
    if (doc.what == D_Name && ni != nullptr) {
        bool ctrl = (ni->controller >= 0 || ni->gen_controller >= 0) &&
                    (doc.controller_first || (ni->trigger < 0 && ni->redirect < 0 && ni->gen_trigger < 0));
        if (ctrl) {
            if (ni->controller >= 0) {
                const NameDoc &d = kControllers[ni->controller];
                ImGui::TextColored(Colour(C_Controller), "%s (state controller)", d.name);
                if (d.syntax[0] != '\0') {
                    ImGui::TextColored(Colour(C_Key), "%s", d.syntax);
                }
                ImGui::TextUnformatted(d.desc);
            } else {
                ImGui::TextColored(Colour(C_Controller), "%s (Ikemen GO state controller)", ni->spelling);
            }
            if (ni->gen_controller >= 0) {
                ShowList("Parameters:", names.ctrl_params[ni->gen_controller], 48);
            }
        } else if (ni->trigger >= 0 || ni->redirect >= 0) {
            const NameDoc &d = ni->trigger >= 0 ? kTriggers[ni->trigger] : kRedirects[ni->redirect];
            ImGui::TextColored(Colour(ni->trigger >= 0 ? C_Trigger : C_Redirect), "%s", d.syntax);
            ImGui::TextUnformatted(d.desc);
        } else if (ni->gen_trigger >= 0) {
            ImGui::TextColored(Colour(C_Trigger), "%s (Ikemen GO trigger)", ni->spelling);
        }
        if (!ctrl && ni->gen_trigger >= 0 && ni->redirect < 0 && !names.trig_args[ni->gen_trigger].empty()) {
            ShowList("Takes:", names.trig_args[ni->gen_trigger], 30);
        }
    } else if (doc.what == D_Param) {
        const NameInfo *ci = Info(doc.ctrl);
        const char *owner = doc.ctrl.empty() ? "the statedef" : ci != nullptr && ci->spelling ? ci->spelling : doc.ctrl.c_str();
        ImGui::TextColored(Colour(C_Key), "%s", doc.name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("a parameter of %s", owner);
        std::string part = ci != nullptr && ci->controller >= 0 ? ParamPart(kControllers[ci->controller].syntax, doc.name) : "";
        const NameInfo *pi = Info(doc.name);
        if (!part.empty()) {
            ImGui::TextColored(Colour(C_Key), "%s", part.c_str());
        } else if (pi != nullptr && pi->param >= 0 && kParamDocs[pi->param].syntax[0] != '\0') {
            ImGui::TextColored(Colour(C_Key), "%s = %s", doc.name.c_str(), kParamDocs[pi->param].syntax);
        }
        if (pi != nullptr && pi->param >= 0) {
            ImGui::TextUnformatted(kParamDocs[pi->param].desc);
        } else if (const NameInfo *t1 = Info("trigger1"); doc.name.compare(0, 7, "trigger") == 0 && t1 && t1->param >= 0) {
            ImGui::TextUnformatted(kParamDocs[t1->param].desc);
        }
    } else if (doc.what == D_Arg) {
        const NameInfo *ti = Info(doc.ctrl);
        ImGui::TextColored(Colour(C_Trigger), "%s(%s)", ti != nullptr && ti->spelling ? ti->spelling : doc.ctrl.c_str(),
                           doc.name.c_str());
        const NameInfo *pi = Info(doc.name);
        if (pi != nullptr && pi->param >= 0) {
            ImGui::TextUnformatted(kParamDocs[pi->param].desc);
        }
    }
    ImGui::PopTextWrapPos();
}

/* Whether a doc has anything to show. */
static bool DocShows(const Doc &doc)
{
    if (doc.what == D_Name) {
        const NameInfo *ni = Info(doc.name);
        return ni != nullptr && HasDoc(*ni);
    }
    return doc.what != D_None;
}

static const char *KindName(const NameInfo &ni)
{
    if ((ni.controller >= 0 || ni.gen_controller >= 0) && ni.redirect >= 0) {
        return "controller, redirection";
    }
    if (ni.controller >= 0 || ni.gen_controller >= 0) {
        return "controller";
    }
    if (ni.redirect >= 0) {
        return "redirection";
    }
    if (ni.trigger >= 0 || ni.gen_trigger >= 0) {
        return "trigger";
    }
    if (ni.keyword) {
        return "keyword";
    }
    return "parameter";
}

/* ------------------------------------------------------------------ completion */

/* What fits there, sorted. */
static void Candidates(const Context &x, bool zss, std::vector<CompItem> &out)
{
    const Names &names = KnownNames();
    out.clear();
    auto name = [&](const Word &w, bool controller_first) {
        CompItem it;
        it.text = zss ? w.zss : w.name;
        it.kind = KindName(names.map.find(w.lower)->second);
        it.doc.what = D_Name;
        it.doc.name = w.lower;
        it.doc.controller_first = controller_first;
        out.push_back(it);
    };
    auto param = [&](const std::string &p, const std::string &ctrl) {
        CompItem it;
        it.text = p;
        it.kind = "parameter";
        it.doc.what = D_Param;
        it.doc.name = p;
        it.doc.ctrl = ctrl;
        out.push_back(it);
    };
    switch (x.kind) {
    case X_Any:
        for (const Word &w : names.words) {
            name(w, false);
        }
        break;
    case X_Controller:
    case X_Statement:
        for (const Word &w : names.words) {
            const NameInfo &ni = names.map.find(w.lower)->second;
            if (ni.controller >= 0 || ni.gen_controller >= 0) {
                name(w, true);
                if (zss) {
                    out.back().tail = "{}";
                }
            }
        }
        if (x.kind == X_Statement) {
            for (const char *k : kZssKeywords) {
                CompItem it;
                it.text = k;
                it.kind = "keyword";
                out.push_back(it);
            }
        }
        break;
    case X_Expr:
        for (const Word &w : names.words) {
            const NameInfo &ni = names.map.find(w.lower)->second;
            if (ni.trigger >= 0 || ni.redirect >= 0 || ni.gen_trigger >= 0) {
                name(w, false);
            }
        }
        break;
    case X_Key: {
        if (x.statedef) {
            for (const std::string &p : names.statedef_params) {
                param(p, "");
            }
            break;
        }
        const NameInfo *ci = Info(x.ctrl);
        if (ci != nullptr && ci->gen_controller >= 0) {
            for (const std::string &p : names.ctrl_params[ci->gen_controller]) {
                param(p, x.ctrl);
            }
        }
        if (!zss) {
            for (const char *p : { "type", "triggerall", "trigger1", "trigger2", "trigger3", "trigger4", "persistent",
                                   "ignorehitpause" }) {
                param(p, x.ctrl);
            }
        }
        break;
    }
    case X_Arg:
        for (const std::string &a : names.trig_args[x.trig]) {
            CompItem it;
            it.text = a;
            it.kind = "name";
            it.doc.what = D_Arg;
            it.doc.name = a;
            it.doc.ctrl = Lower(kGenTriggers[x.trig].name, strlen(kGenTriggers[x.trig].name));
            out.push_back(it);
        }
        break;
    default:
        break;
    }
    std::stable_sort(out.begin(), out.end(), [](const CompItem &a, const CompItem &b) {
        return Lower(a.text.data(), a.text.size()) < Lower(b.text.data(), b.text.size());
    });
    out.erase(std::unique(out.begin(), out.end(), [](const CompItem &a, const CompItem &b) { return a.text == b.text; }),
              out.end());
    /* a parameter is followed by its '=' (CNS) or ':' (ZSS) */
    if (x.kind == X_Key) {
        for (CompItem &it : out) {
            it.tail = zss ? ": " : " = ";
        }
    }
}

/* Those that match word: those that start with it first, then those that contain it. */
static void FindCompletions(const std::string &word, std::vector<CompItem> &items)
{
    std::string lw = Lower(word.data(), word.size());
    std::vector<CompItem> first, inside;
    for (CompItem &it : items) {
        size_t at = Lower(it.text.data(), it.text.size()).find(lw);
        if (at == 0) {
            first.push_back(std::move(it));
        } else if (at != std::string::npos) {
            inside.push_back(std::move(it));
        }
    }
    first.insert(first.end(), std::make_move_iterator(inside.begin()), std::make_move_iterator(inside.end()));
    if (first.size() > 400) {
        first.resize(400);
    }
    items = std::move(first);
}

/*
 * Opens (or updates) the completion of the word before the cursor; closes it when there is none.
 * force: 0 a word of 2 letters at least (1 for a parameter, or a name in parentheses); 1 also
 * none yet where a parameter or a name in parentheses goes ('(' or '{' typed); 2 anything (Ctrl+Space).
 */
static void UpdateCompletion(CodeEditor *ed, bool open, int force = 0)
{
    if (!open && !ed->comp_open) {
        return;
    }
    const std::string &t = *ed->text;
    if (HasSelection(ed)) {
        CloseCompletion(ed);
        return;
    }
    int l = LineOf(ed, ed->cur);
    size_t lb = ed->lines[l];
    size_t wb = ed->cur;
    while (wb > lb && IsIdent(t[wb - 1])) {
        wb--;
    }
    if (wb < ed->cur && !(IsAlpha(t[wb]) || t[wb] == '_')) {
        CloseCompletion(ed);
        return;
    }
    /* not in comments, strings or headers */
    const char *s = t.data() + lb;
    size_t n = LineEnd(ed, t, l) - lb;
    Highlight(s, n, ed->colors, ed->zss);
    int col = ed->colors[wb - lb];
    if (wb > lb && ed->colors[wb - lb - 1] == C_Comment) {
        col = C_Comment;    /* at the end of a comment */
    }
    if (col == C_Comment || col == C_String || col == C_Header) {
        CloseCompletion(ed);
        return;
    }
    Context x = ContextAt(ed, t, wb);
    size_t need = (x.kind == X_Key || x.kind == X_Arg) ? 1 : 2;
    if (force == 2 || (force == 1 && (x.kind == X_Key || x.kind == X_Arg))) {
        need = 0;
    }
    if (x.kind == X_None || ed->cur - wb < need) {
        CloseCompletion(ed);
        return;
    }
    std::string word = t.substr(wb, ed->cur - wb);
    Candidates(x, ed->zss, ed->comp_items);
    FindCompletions(word, ed->comp_items);
    if (ed->comp_items.empty() ||
        (ed->comp_items.size() == 1 && ed->comp_items[0].tail.empty() && Lower(word.data(), word.size()) ==
                                           Lower(ed->comp_items[0].text.data(), ed->comp_items[0].text.size()))) {
        CloseCompletion(ed);
        return;
    }
    ed->comp_open = true;
    ed->comp_begin = wb;
    ed->comp_sel = 0;
    ed->comp_top = 0;
    ed->comp_moved = false;
}

/* Enter/Tab with the list open: whether they take the selected name (or pass, the word is already it). */
static bool CompletionTakesKey(const CodeEditor *ed)
{
    if (!ed->comp_open || ed->comp_items.empty()) {
        return false;
    }
    if (ed->comp_moved) {
        return true;
    }
    /* a parameter typed whole still takes its ' = ' or ': ' */
    const CompItem &it = ed->comp_items[ed->comp_sel];
    return !it.tail.empty() || ed->text->compare(ed->comp_begin, ed->cur - ed->comp_begin, it.text) != 0;
}

static void AcceptCompletion(CodeEditor *ed)
{
    CompItem it = ed->comp_items[ed->comp_sel];
    const std::string &t = *ed->text;
    size_t begin = ed->comp_begin, end = ed->cur;
    /* the rest of the word goes too; its tail only when what follows is not it already */
    while (end < t.size() && IsIdent(t[end])) {
        end++;
    }
    size_t next = end;
    while (next < t.size() && IsBlank(t[next])) {
        next++;
    }
    std::string tail = it.tail;
    std::string tail_trim = tail;
    tail_trim.erase(0, tail_trim.find_first_not_of(' '));
    if (!tail_trim.empty() && next < t.size() && t[next] == tail_trim[0]) {
        tail.clear();
    }
    CloseCompletion(ed);
    ed->anchor = ed->cur;
    Replace(ed, begin, end, it.text + tail, G_None);
    if (!tail.empty() && tail.back() == '}') {
        ed->cur = ed->anchor = ed->cur - 1;
        RecordAfter(ed);
        UpdateCompletion(ed, true, 1);  /* its parameters */
    }
    ed->merge = false;
}

/* The list of names, under the word; it takes no input (the keys stay with the editor). */
static void DrawCompletion(CodeEditor *ed)
{
    int count = (int)ed->comp_items.size();
    int rows = std::min(count, 10);
    if (ed->comp_sel < ed->comp_top) {
        ed->comp_top = ed->comp_sel;
    }
    if (ed->comp_sel >= ed->comp_top + rows) {
        ed->comp_top = ed->comp_sel - rows + 1;
    }
    ed->comp_top = std::max(0, std::min(ed->comp_top, count - rows));

    /* below the line, or above it near the bottom of the screen */
    ImGuiViewport *vp = ImGui::GetWindowViewport();
    float below = ed->comp_pos.y + ImGui::GetFontSize() + 4.0f;
    float h = (rows + 4) * ImGui::GetTextLineHeightWithSpacing();
    if (below + h > vp->WorkPos.y + vp->WorkSize.y && ed->comp_pos.y - h > vp->WorkPos.y) {
        ImGui::SetNextWindowPos(ImVec2(ed->comp_pos.x, ed->comp_pos.y - 2.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    } else {
        ImGui::SetNextWindowPos(ImVec2(ed->comp_pos.x, below), ImGuiCond_Always);
    }
    char name[64];
    ImFormatString(name, sizeof(name), "##completion%08X", ed->id);
    ImGuiWindowFlags flags = ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(name, nullptr, flags)) {
        float w = 0.0f;
        for (int i = ed->comp_top; i < ed->comp_top + rows; i++) {
            w = std::max(w, ImGui::CalcTextSize(ed->comp_items[i].text.c_str()).x);
        }
        w += ImGui::GetFontSize();
        for (int i = ed->comp_top; i < ed->comp_top + rows; i++) {
            const CompItem &it = ed->comp_items[i];
            ImGui::PushID(i);
            ImGui::Selectable(it.text.c_str(), i == ed->comp_sel, ImGuiSelectableFlags_None, ImVec2(w, 0.0f));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", it.kind);
            ImGui::PopID();
        }
        if (count > rows) {
            ImGui::TextDisabled("%d of %d", ed->comp_sel + 1, count);
        }
        const CompItem &sel = ed->comp_items[ed->comp_sel];
        if (DocShows(sel.doc)) {
            ImGui::Separator();
            ShowDoc(sel.doc);
        }
        ImGui::TextDisabled("Enter/Tab: take it, Esc: close, Ctrl+Space: all that fit here");
    }
    ImGui::End();
}

/* ------------------------------------------------------------------ input */

/* The keyboard, while the editor has the focus (and the active id). */
static void HandleKeys(CodeEditor *ed, const Metrics &m, float view_h)
{
    ImGuiIO &io = ImGui::GetIO();
    std::string &t = *ed->text;
    ImGuiID id = ed->id;
    const bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
    auto key = [id](ImGuiKey k) { return ImGui::IsKeyPressed(k, ImGuiInputFlags_Repeat, id); };

    /* typed characters (AltGr is Ctrl+Alt); 2: the last one opens a list of parameters or names */
    int typed = 0;
    if (io.InputQueueCharacters.Size > 0) {
        if (!ctrl || alt) {
            std::string s;
            for (ImWchar w : io.InputQueueCharacters) {
                unsigned int c = w;
                if (c < 32 || c == 127) {
                    continue;   /* Tab and Enter come as keys */
                }
                char buf[5];
                int n = ImTextCharToUtf8(buf, c);
                s.append(buf, (size_t)n);
            }
            if (!s.empty()) {
                InsertText(ed, s, G_Type);
                typed = s.back() == '(' || s.back() == '{' ? 2 : 1;
            }
        }
        io.InputQueueCharacters.resize(0);
    }
    if (typed) {
        UpdateCompletion(ed, true, typed == 2 ? 1 : 0);
    }
    if (ctrl && !alt && key(ImGuiKey_Space)) {
        UpdateCompletion(ed, true, 2);
        return;
    }

    /* the completion list takes Up, Down, Enter, Tab and Escape */
    if (ed->comp_open) {
        int count = (int)ed->comp_items.size();
        if (key(ImGuiKey_DownArrow)) {
            ed->comp_sel = (ed->comp_sel + 1) % count;
            ed->comp_moved = true;
            return;
        }
        if (key(ImGuiKey_UpArrow)) {
            ed->comp_sel = (ed->comp_sel + count - 1) % count;
            ed->comp_moved = true;
            return;
        }
        if (key(ImGuiKey_PageDown)) {
            ed->comp_sel = std::min(ed->comp_sel + 9, count - 1);
            ed->comp_moved = true;
            return;
        }
        if (key(ImGuiKey_PageUp)) {
            ed->comp_sel = std::max(ed->comp_sel - 9, 0);
            ed->comp_moved = true;
            return;
        }
        if ((key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter) || key(ImGuiKey_Tab)) && !ctrl) {
            if (CompletionTakesKey(ed)) {
                AcceptCompletion(ed);
                return;
            }
            CloseCompletion(ed);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, ImGuiInputFlags_None, id)) {
            CloseCompletion(ed);
            ImGui::SetKeyOwner(ImGuiKey_Escape, id, ImGuiInputFlags_LockThisFrame);
            return;
        }
    }

    int line = LineOf(ed, ed->cur);
    int rows = std::max(1, (int)(view_h / m.line_h) - 1);
    if (ctrl && !alt && key(ImGuiKey_A)) {
        ed->anchor = 0;
        ed->cur = t.size();
        ed->merge = false;
        CloseCompletion(ed);
    } else if ((ctrl && key(ImGuiKey_C)) || (ctrl && key(ImGuiKey_Insert))) {
        Copy(ed);
    } else if ((ctrl && key(ImGuiKey_X)) || (shift && key(ImGuiKey_Delete))) {
        if (HasSelection(ed)) {
            Copy(ed);
            InsertText(ed, std::string(), G_None);
        }
        CloseCompletion(ed);
    } else if ((ctrl && key(ImGuiKey_V)) || (shift && key(ImGuiKey_Insert))) {
        Paste(ed);
        CloseCompletion(ed);
    } else if ((ctrl && shift && key(ImGuiKey_Z)) || (ctrl && key(ImGuiKey_Y))) {
        Redo(ed);
        CloseCompletion(ed);
    } else if (ctrl && key(ImGuiKey_Z)) {
        Undo(ed);
        CloseCompletion(ed);
    } else if (ctrl && key(ImGuiKey_D)) {
        DuplicateLines(ed);
        CloseCompletion(ed);
    } else if (ctrl && (key(ImGuiKey_0) || key(ImGuiKey_Keypad0))) {
        Editor_SetZoom(1.0f);   /* the text size back to the font's */
    } else if (ctrl && shift && key(ImGuiKey_K)) {
        DeleteLines(ed, m);
        CloseCompletion(ed);
    } else if (ctrl && (key(ImGuiKey_Slash) || key(ImGuiKey_KeypadDivide) || key(ImGuiKey_Semicolon))) {
        ToggleComment(ed);
        CloseCompletion(ed);
    } else if (key(ImGuiKey_LeftArrow)) {
        if (HasSelection(ed) && !shift) {
            MoveTo(ed, SelBegin(ed), false);
        } else {
            MoveTo(ed, ctrl ? WordLeft(ed, t, ed->cur) : PrevChar(ed, t, ed->cur), shift);
        }
    } else if (key(ImGuiKey_RightArrow)) {
        if (HasSelection(ed) && !shift) {
            MoveTo(ed, SelEnd(ed), false);
        } else {
            MoveTo(ed, ctrl ? WordRight(ed, t, ed->cur) : NextChar(ed, t, ed->cur), shift);
        }
    } else if (key(ImGuiKey_UpArrow)) {
        if (ctrl) {
            ed->scroll_dy -= m.line_h;
        } else {
            if (ed->want_x < 0.0f) {
                ed->want_x = XOf(ed, m, t, ed->cur);
            }
            MoveTo(ed, line > 0 ? PosAtX(ed, m, t, line - 1, ed->want_x) : 0, shift, true);
        }
    } else if (key(ImGuiKey_DownArrow)) {
        if (ctrl) {
            ed->scroll_dy += m.line_h;
        } else {
            if (ed->want_x < 0.0f) {
                ed->want_x = XOf(ed, m, t, ed->cur);
            }
            MoveTo(ed, line + 1 < LineCount(ed) ? PosAtX(ed, m, t, line + 1, ed->want_x) : t.size(), shift, true);
        }
    } else if (key(ImGuiKey_PageUp) || key(ImGuiKey_PageDown)) {
        int dir = ImGui::IsKeyDown(ImGuiKey_PageUp) ? -1 : 1;
        if (ed->want_x < 0.0f) {
            ed->want_x = XOf(ed, m, t, ed->cur);
        }
        int to = std::max(0, std::min(LineCount(ed) - 1, line + dir * rows));
        MoveTo(ed, PosAtX(ed, m, t, to, ed->want_x), shift, true);
        ed->scroll_dy += (float)((to - line) * m.line_h);
    } else if (key(ImGuiKey_Home)) {
        if (ctrl) {
            MoveTo(ed, 0, shift);
        } else {
            size_t ind = LineIndentEnd(ed, t, line);
            MoveTo(ed, ed->cur == ind ? ed->lines[line] : ind, shift);
        }
    } else if (key(ImGuiKey_End)) {
        MoveTo(ed, ctrl ? t.size() : LineEnd(ed, t, line), shift);
    } else if (key(ImGuiKey_Backspace)) {
        if (HasSelection(ed)) {
            InsertText(ed, std::string(), G_None);
        } else if (ed->cur > 0) {
            size_t from = ctrl ? WordLeft(ed, t, ed->cur) : PrevChar(ed, t, ed->cur);
            Replace(ed, from, ed->cur, std::string(), ctrl ? G_None : G_Backspace);
        }
        UpdateCompletion(ed, false);
    } else if (key(ImGuiKey_Delete)) {
        if (HasSelection(ed)) {
            InsertText(ed, std::string(), G_None);
        } else if (ed->cur < t.size()) {
            size_t to = ctrl ? WordRight(ed, t, ed->cur) : NextChar(ed, t, ed->cur);
            Replace(ed, ed->cur, to, std::string(), ctrl ? G_None : G_Delete);
        }
        CloseCompletion(ed);
    } else if ((key(ImGuiKey_Enter) || key(ImGuiKey_KeypadEnter)) && !ctrl) {
        /* the new line keeps the indentation of this one */
        int sl = LineOf(ed, SelBegin(ed));
        size_t lb = ed->lines[sl];
        size_t ind = std::min(LineIndentEnd(ed, t, sl), SelBegin(ed));
        InsertText(ed, "\n" + t.substr(lb, ind - lb), G_None);
        CloseCompletion(ed);
    } else if (key(ImGuiKey_Tab) && !ctrl && !alt) {
        int a, b;
        SelLines(ed, &a, &b);
        if (shift || (HasSelection(ed) && b > a)) {
            IndentLines(ed, a, b, shift);
        } else {
            InsertText(ed, IndentUnit(ed, t, line), G_None);
        }
        CloseCompletion(ed);
    }
}

/* The place of the text under the mouse. */
static size_t PosAtMouse(const CodeEditor *ed, const Metrics &m, ImVec2 mouse, ImVec2 origin)
{
    int l = (int)floorf((mouse.y - origin.y) / m.line_h);
    l = std::max(0, std::min(LineCount(ed) - 1, l));
    return PosAtX(ed, m, *ed->text, l, mouse.x - origin.x);
}

/* Takes the keyboard: the active id with all the keyboard keys, like InputText. */
static void TakeFocus(CodeEditor *ed, ImGuiWindow *win)
{
    ImGuiContext &g = *GImGui;
    if (g.ActiveId != ed->id) {
        ImGui::SetActiveID(ed->id, win);
    }
    ImGui::SetFocusID(ed->id, win);
    ImGui::FocusWindow(win);
    ed->focused = true;
    ed->blink = -0.3f;
}

/* Brings the text to the editor: the first time, or when the caller changed it. */
static void Sync(CodeEditor *ed, std::string &text, unsigned int version)
{
    if (ed->seen == &text && ed->seen_version == version && ed->seen_size == text.size() && !ed->lines.empty()) {
        return;
    }
    /* keep the line and column of the cursor */
    int line = 0;
    size_t col = 0;
    if (!ed->lines.empty()) {
        line = LineOf(ed, ed->cur);
        col = ed->cur - ed->lines[line];
    }
    ed->text = &text;
    BuildLines(ed, text);
    line = std::min(line, LineCount(ed) - 1);
    ed->cur = ed->anchor = std::min(ed->lines[line] + col, LineEnd(ed, text, line));
    ed->undo.clear();
    ed->undo_done = 0;
    ed->merge = false;
    ed->dragging = false;
    ed->want_x = -1.0f;
    CloseCompletion(ed);
    ed->seen = &text;
    ed->seen_version = version;
    ed->seen_size = text.size();
}

/* Scrolls so that the cursor is in view (centred for mode 2, when it is not in view). */
static void ScrollToCursor(CodeEditor *ed, const Metrics &m, ImGuiWindow *win, const ImRect &area, float text_w,
                           float max_x, float max_y)
{
    const std::string &t = *ed->text;
    float top = LineOf(ed, ed->cur) * m.line_h;
    float h = area.GetHeight();
    float sy = win->Scroll.y + ed->scroll_dy;
    if (ed->scroll == 2) {
        if (top < sy || top + m.line_h > sy + h) {
            sy = top - h * 0.33f;
        }
    } else if (ed->scroll == 1) {
        if (top < sy) {
            sy = top;
        } else if (top + m.line_h > sy + h) {
            sy = top + m.line_h - h;
        }
    }
    float sx = win->Scroll.x;
    if (ed->scroll != 0) {
        float x = XOf(ed, m, t, ed->cur);
        if (x < sx) {
            sx = std::max(0.0f, x - m.space * 8.0f);
        } else if (x > sx + text_w - m.space * 2.0f) {
            sx = x - text_w + m.space * 8.0f;
        }
    }
    win->Scroll.x = floorf(std::max(0.0f, std::min(sx, max_x)));
    win->Scroll.y = floorf(std::max(0.0f, std::min(sy, max_y)));
    ed->scroll = 0;
    ed->scroll_dy = 0.0f;
}

/* ------------------------------------------------------------------ drawing */

/* The text of a line, from the x of the left of the view to its right. */
static void DrawLine(ImDrawList *dl, const Metrics &m, const char *s, const char *e, const unsigned char *colors,
                     ImVec2 pos, float clip_l, float clip_r)
{
    const char *b = s;
    float x = 0.0f;
    float y = pos.y + floorf((m.line_h - m.size) * 0.5f);
    while (s < e) {
        unsigned int c;
        int n = CharAt(s, e, &c);
        float a = Advance(m, c, x);
        float sx = pos.x + x;
        if (sx > clip_r) {
            break;
        }
        if (sx + a >= clip_l && c != ' ' && c != '\t' && c != '\r') {
            m.font->RenderChar(dl, m.size, ImVec2(sx, y), pal->syntax[colors[s - b]], (ImWchar)c);
        }
        x += a;
        s += n;
    }
}

static void Measure(CodeEditor *ed, const Metrics &m)
{
    const std::string &t = *ed->text;
    float w = 0.0f;
    for (int l = 0; l < LineCount(ed); l++) {
        w = std::max(w, TextX(m, t.data() + ed->lines[l], t.data() + LineEnd(ed, t, l), t.data() + t.size()));
    }
    ed->max_width = w;
    ed->measured_size = m.size;
}

/* ------------------------------------------------------------------ API */

CodeEditor *Editor_Create()
{
    return new CodeEditor();
}

void Editor_Destroy(CodeEditor *ed)
{
    delete ed;
}

bool Editor_Draw(CodeEditor *ed, const char *id, std::string &text, unsigned int &version, ImVec2 size)
{
    ImGuiContext &g = *GImGui;
    ImGuiIO &io = g.IO;
    ed->changed = false;
    Sync(ed, text, version);
    ed->text = &text;

    /* what Editor_GotoLine and Editor_Select asked for */
    if (ed->goto_line > 0) {
        int l = std::max(0, std::min(LineCount(ed) - 1, ed->goto_line - 1));
        ed->anchor = ed->lines[l];
        ed->cur = ed->goto_select ? LineEnd(ed, text, l) : ed->lines[l];
        ed->goto_line = 0;
        ed->scroll = 2;
        ed->want_x = -1.0f;
        CloseCompletion(ed);
    }
    if (ed->sel_begin != kNone) {
        ed->anchor = ClampPos(ed, text, ed->sel_begin);
        ed->cur = ClampPos(ed, text, ed->sel_end);
        ed->sel_begin = ed->sel_end = kNone;
        ed->scroll = 2;
        ed->want_x = -1.0f;
        CloseCompletion(ed);
    }

    /* the editors' own text size (popped before each return) */
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * zoom);
    Metrics m;
    InitMetrics(&m);
    if (ed->measured_size != m.size) {
        Measure(ed, m);
    }
    int digits = 1;
    for (int n = LineCount(ed); n >= 10; n /= 10) {
        digits++;
    }
    float mark_w = ed->mark_of ? floorf(m.line_h) : 0.0f;
    float gutter = mark_w + std::max(digits, 3) * m.adv['0'] + m.space * 2.0f + 4.0f;
    float content_w = gutter + kMargin + ed->max_width + m.space * 8.0f;
    float content_h = (LineCount(ed) + 2) * m.line_h;
    ed->mark_clicked = 0;
    ed->ctrl_clicked = kNone;

    ImGui::SetNextWindowContentSize(ImVec2(content_w, content_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, pal->background);
    bool visible = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGuiWindow *win = g.CurrentWindow;
    ed->id = win->GetID("##text");
    ImGuiID wid = ed->id;
    ImRect area = win->InnerClipRect;
    if (!visible || area.GetWidth() <= 0.0f || area.GetHeight() <= 0.0f) {
        ImGui::EndChild();
        ImGui::PopFont();
        return false;
    }
    float text_w = area.GetWidth() - gutter - kMargin;
    float max_x = std::max(0.0f, content_w - win->InnerRect.GetWidth());
    float max_y = std::max(0.0f, content_h - win->InnerRect.GetHeight());

    ImGui::ItemAdd(area, wid);
    bool hovered = ImGui::ItemHoverable(area, wid, ImGuiItemFlags_None);

    /*
     * Ctrl+Wheel: the text size. While Ctrl is held over the text the wheel is ours, so that
     * it does not scroll too (taken a frame ahead: ImGui scrolls at the start of the frame).
     */
    /* (the mouse over the text: not hovered, which keyboard navigation turns off until the mouse moves) */
    bool zooming = io.KeyCtrl && g.HoveredWindow == win && ImGui::IsMouseHoveringRect(area.Min, area.Max);
    if (zooming) {
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, wid);
        if (io.MouseWheel != 0.0f) {
            /* the same lines in view: the scroll grows with the text */
            float before = zoom;
            Editor_SetZoom(zoom * powf(1.1f, io.MouseWheel));
            win->Scroll.x = floorf(win->Scroll.x * zoom / before);
            win->Scroll.y = floorf(win->Scroll.y * zoom / before);
        }
    } else if (ImGui::GetKeyOwner(ImGuiKey_MouseWheelY) == wid) {
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGuiKeyOwner_NoOwner);
    }

    /*
     * The focus. Our own scrollbars take the active id while dragged: the
     * focus comes back after. Anything else that takes it (another widget, a
     * click elsewhere) takes the focus.
     */
    if (ed->focused && g.ActiveId != wid) {
        ImGuiID sx = ImGui::GetWindowScrollbarID(win, ImGuiAxis_X), sy = ImGui::GetWindowScrollbarID(win, ImGuiAxis_Y);
        if (g.ActiveId == 0 && (g.ActiveIdPreviousFrame == sx || g.ActiveIdPreviousFrame == sy)) {
            ImGui::SetActiveID(wid, win);
        } else if (g.ActiveId != sx && g.ActiveId != sy) {
            ed->focused = false;
            ed->dragging = false;
            CloseCompletion(ed);
        }
    }
    bool clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    if (ed->focused && g.ActiveId == wid && io.MouseClicked[0] && !hovered) {
        ImGui::ClearActiveID();
        ed->focused = false;
        ed->dragging = false;
        CloseCompletion(ed);
    }
    if (ed->want_focus) {
        TakeFocus(ed, win);
        ed->want_focus = false;
    }
    ImVec2 origin(area.Min.x + gutter + kMargin - win->Scroll.x, area.Min.y - win->Scroll.y);
    if (clicked) {
        TakeFocus(ed, win);
        ImGui::SetKeyOwner(ImGuiKey_MouseLeft, wid);
        size_t p = PosAtMouse(ed, m, io.MousePos, origin);
        int clicks = io.MouseClickedCount[0];
        int l = LineOf(ed, p);
        if (io.MousePos.x < area.Min.x + mark_w) {
            /* the marks: the caller's (Editor_MarkClicked) */
            int ml = (int)floorf((io.MousePos.y - origin.y) / m.line_h);
            if (ml >= 0 && ml < LineCount(ed)) {
                ed->mark_clicked = ml + 1;
            }
        } else if (io.KeyShift) {
            ed->cur = p;
            ed->dragging = true;
        } else if (io.KeyCtrl && clicks == 1 && io.MousePos.x >= area.Min.x + gutter) {
            /* the caller's (Editor_CtrlClicked: go to a definition) */
            ed->cur = ed->anchor = p;
            ed->ctrl_clicked = p;
            ed->dragging = false;
        } else if (io.MousePos.x < area.Min.x + gutter) {
            /* the line numbers: the whole line */
            ed->anchor = ed->lines[l];
            ed->cur = l + 1 < LineCount(ed) ? ed->lines[l + 1] : LineEnd(ed, text, l);
            ed->dragging = false;
        } else if (clicks == 2) {
            SelectWord(ed, p);
            ed->dragging = false;
        } else if (clicks >= 3) {
            ed->anchor = ed->lines[l];
            ed->cur = l + 1 < LineCount(ed) ? ed->lines[l + 1] : LineEnd(ed, text, l);
            ed->dragging = false;
        } else {
            ed->cur = ed->anchor = p;
            ed->dragging = true;
        }
        ed->want_x = -1.0f;
        ed->merge = false;
        ed->blink = -0.3f;
        CloseCompletion(ed);
    }
    bool active = ed->focused && g.ActiveId == wid;
    if (active) {
        ImGui::SetActiveIdUsingAllKeyboardKeys();
        g.ActiveIdAllowOverlap = !io.MouseDown[0];
        HandleKeys(ed, m, area.GetHeight());
    }

    /* dragging a selection, scrolling near the edges */
    if (ed->dragging) {
        if (!active || !io.MouseDown[0]) {
            ed->dragging = false;
        } else {
            ImVec2 mp = io.MousePos;
            float speed = io.DeltaTime * 12.0f;
            float dx = 0.0f, dy = 0.0f;
            if (mp.y < area.Min.y) {
                dy = (mp.y - area.Min.y) * speed - 1.0f;
            } else if (mp.y > area.Max.y) {
                dy = (mp.y - area.Max.y) * speed + 1.0f;
            }
            if (mp.x < area.Min.x + gutter) {
                dx = (mp.x - area.Min.x - gutter) * speed - 1.0f;
            } else if (mp.x > area.Max.x) {
                dx = (mp.x - area.Max.x) * speed + 1.0f;
            }
            if (dx != 0.0f || dy != 0.0f) {
                win->Scroll.x = floorf(std::max(0.0f, std::min(win->Scroll.x + dx, max_x)));
                win->Scroll.y = floorf(std::max(0.0f, std::min(win->Scroll.y + dy, max_y)));
            }
            origin = ImVec2(area.Min.x + gutter + kMargin - win->Scroll.x, area.Min.y - win->Scroll.y);
            size_t p = PosAtMouse(ed, m, mp, origin);
            if (p != ed->cur) {
                ed->cur = p;
                ed->blink = -0.3f;
            }
        }
    }
    if (ed->measured_size != m.size) {
        Measure(ed, m);
        content_w = gutter + kMargin + ed->max_width + m.space * 8.0f;
        max_x = std::max(0.0f, content_w - win->InnerRect.GetWidth());
    }
    content_h = (LineCount(ed) + 2) * m.line_h;
    max_y = std::max(0.0f, content_h - win->InnerRect.GetHeight());
    if (ed->scroll != 0 || ed->scroll_dy != 0.0f) {
        ScrollToCursor(ed, m, win, area, text_w, max_x, max_y);
    }
    origin = ImVec2(area.Min.x + gutter + kMargin - win->Scroll.x, area.Min.y - win->Scroll.y);

    if (hovered) {
        ImGui::SetMouseCursor(io.MousePos.x < area.Min.x + gutter ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_TextInput);
    }

    /* the lines in view */
    ImDrawList *dl = win->DrawList;
    const std::string &t = text;
    const char *d = t.data();
    int count = LineCount(ed);
    int first = std::max(0, (int)floorf((area.Min.y - origin.y) / m.line_h));
    int last = std::min(count - 1, (int)floorf((area.Max.y - origin.y) / m.line_h));
    int cur_line = LineOf(ed, ed->cur);
    size_t sb = SelBegin(ed), se = SelEnd(ed);
    ImVec2 text_min(area.Min.x + gutter, area.Min.y);
    dl->PushClipRect(text_min, area.Max, true);
    for (int l = first; l <= last; l++) {
        float y = origin.y + l * m.line_h;
        size_t lb = ed->lines[l], le = LineEnd(ed, t, l);
        if (ed->background_of) {
            if (ImU32 bg = ed->background_of(l + 1)) {
                dl->AddRectFilled(ImVec2(text_min.x, y), ImVec2(area.Max.x, y + m.line_h), bg);
            }
        }
        if (l + 1 == ed->error_line) {
            dl->AddRectFilled(ImVec2(text_min.x, y), ImVec2(area.Max.x, y + m.line_h), pal->error_bg);
        } else if (l == cur_line && active && sb == se) {
            dl->AddRectFilled(ImVec2(text_min.x, y), ImVec2(area.Max.x, y + m.line_h), pal->current_line);
        }
        if (!ed->highlights.empty()) {
            /* the found text on this line */
            auto it = std::lower_bound(ed->highlights.begin(), ed->highlights.end(), std::make_pair(lb, (size_t)0),
                                       [](const std::pair<size_t, size_t> &a, const std::pair<size_t, size_t> &b) {
                                           return a.second <= b.first;
                                       });
            for (; it != ed->highlights.end() && it->first <= le; ++it) {
                size_t a = std::max(it->first, lb), b = std::min(it->second, le);
                float xa = origin.x + TextX(m, d + lb, d + le, d + a);
                float xb = std::max(xa + 2.0f, origin.x + TextX(m, d + lb, d + le, d + b));
                bool current = (int)(it - ed->highlights.begin()) == ed->highlight_cur;
                dl->AddRectFilled(ImVec2(xa, y), ImVec2(xb, y + m.line_h),
                                  current ? IM_COL32(230, 160, 30, 150) : IM_COL32(200, 150, 40, 70));
            }
        }
        if (sb != se && sb <= le && se > lb) {
            size_t a = std::max(sb, lb), b = std::min(se, le);
            float xa = origin.x + TextX(m, d + lb, d + le, d + a);
            float xb = origin.x + TextX(m, d + lb, d + le, d + b);
            if (se > le) {
                xb += m.space * 0.5f;   /* the line end is selected too */
            }
            dl->AddRectFilled(ImVec2(xa, y), ImVec2(xb, y + m.line_h), pal->selection);
        }
        Highlight(d + lb, le - lb, ed->colors, ed->zss);
        DrawLine(dl, m, d + lb, d + le, ed->colors.data(), ImVec2(origin.x, y), text_min.x, area.Max.x);
        if (ed->note_of) {
            ImU32 note_col = 0;
            std::string note = ed->note_of(l + 1, &note_col);
            if (!note.empty()) {
                /* after the text, or over its end at the right edge (on a background) when it is too long */
                float w = ImGui::CalcTextSize(note.c_str()).x;
                float x = origin.x + TextX(m, d + lb, d + le, d + le) + m.space * 2.0f;
                float right = area.Max.x - ImGui::GetStyle().ScrollbarSize - m.space - w;
                if (x > right && right > text_min.x) {
                    x = right;
                    dl->AddRectFilled(ImVec2(x - m.space, y), ImVec2(x + w + m.space, y + m.line_h), (pal->background & ~IM_COL32_A_MASK) | IM_COL32(0, 0, 0, 235));
                }
                dl->AddText(ImVec2(x, y + floorf((m.line_h - m.size) * 0.5f)), note_col, note.c_str());
            }
        }
    }

    /* the cursor */
    float cx = origin.x + XOf(ed, m, t, ed->cur);
    float cy = origin.y + cur_line * m.line_h;
    if (active) {
        ed->blink += io.DeltaTime;
        bool on = !io.ConfigInputTextCursorBlink || ed->blink <= 0.0f || fmodf(ed->blink, 1.2f) <= 0.8f;
        if (on) {
            dl->AddRectFilled(ImVec2(floorf(cx), cy), ImVec2(floorf(cx) + 1.5f, cy + m.line_h), pal->cursor);
        }
        /* where the IME goes; io.WantTextInput from the next frame */
        ImGuiPlatformImeData *ime = &g.PlatformImeData;
        ime->WantVisible = true;
        ime->WantTextInput = true;
        ime->InputPos = ImVec2(cx - 1.0f, cy);
        ime->InputLineHeight = m.line_h;
        ime->ViewportId = win->Viewport->ID;
    }
    dl->PopClipRect();
    if (ed->comp_open) {
        ed->comp_pos = ImVec2(origin.x + XOf(ed, m, t, ed->comp_begin) - ImGui::GetStyle().WindowPadding.x, cy);
    }

    /* the line numbers */
    dl->AddRectFilled(area.Min, ImVec2(area.Min.x + gutter, area.Max.y), pal->gutter_bg);
    for (int l = first; l <= last; l++) {
        float y = origin.y + l * m.line_h;
        char num[16];
        int len = ImFormatString(num, sizeof(num), "%d", l + 1);
        float w = len * m.adv['0'];
        if (l + 1 == ed->error_line) {
            dl->AddRectFilled(ImVec2(area.Min.x, y), ImVec2(area.Min.x + 4.0f, y + m.line_h), pal->error_mark);
        }
        if (ed->mark_of) {
            ImVec2 c(area.Min.x + mark_w * 0.5f + 2.0f, y + m.line_h * 0.5f);
            switch (ed->mark_of(l + 1)) {
            case 1: dl->AddCircleFilled(c, m.line_h * 0.12f, IM_COL32(150, 150, 150, 200)); break;
            case 2: dl->AddCircleFilled(c, m.line_h * 0.32f, IM_COL32(220, 60, 60, 255)); break;
            case 3: dl->AddCircleFilled(c, m.line_h * 0.32f, IM_COL32(140, 90, 90, 255)); break;
            default: break;
            }
        }
        ImU32 col = l + 1 == ed->error_line ? pal->error_mark : l == cur_line ? pal->line_number_cur : pal->line_number;
        dl->AddText(ImVec2(area.Min.x + gutter - m.space - w, y + floorf((m.line_h - m.size) * 0.5f)), col, num,
                    num + len);
    }

    /* tooltips: the error of the line, the documentation of the name under the mouse, the caller's note on it */
    if (hovered && !ed->dragging && !io.MouseDown[0]) {
        ImVec2 mp = io.MousePos;
        int l = (int)floorf((mp.y - origin.y) / m.line_h);
        bool on_error = l >= 0 && l < count && l + 1 == ed->error_line && !ed->error_msg.empty();
        Doc doc;
        size_t word = kNone, word_end = kNone;
        if (l >= 0 && l < count && mp.x >= area.Min.x + gutter) {
            size_t lb = ed->lines[l], le = LineEnd(ed, t, l);
            size_t p = PosAtX(ed, m, t, l, mp.x - origin.x);
            float pe = origin.x + TextX(m, d + lb, d + le, d + le);
            /* the character under the mouse (PosAtX gives the nearest boundary) */
            if (p > lb && (p == le || origin.x + TextX(m, d + lb, d + le, d + p) > mp.x)) {
                p--;
            }
            if (mp.x < pe && p < le && IsIdent(t[p])) {
                size_t wb = p, we = p;
                while (wb > lb && IsIdent(t[wb - 1])) {
                    wb--;
                }
                while (we < le && IsIdent(t[we])) {
                    we++;
                }
                if (io.KeyCtrl) {
                    /* Ctrl+click goes to what it names: underlined */
                    float y = origin.y + (l + 1) * m.line_h - 2.0f;
                    dl->AddLine(ImVec2(origin.x + TextX(m, d + lb, d + le, d + wb), y),
                                ImVec2(origin.x + TextX(m, d + lb, d + le, d + we), y), pal->cursor, 1.0f);
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
                word = wb, word_end = we;
                Highlight(d + lb, le - lb, ed->colors, ed->zss);
                int col = ed->colors[wb - lb];
                if (col == C_Trigger || col == C_Redirect || col == C_Controller) {
                    doc.what = D_Name;
                    doc.name = Lower(d + wb, we - wb);
                    doc.controller_first = col == C_Controller;
                } else if (col == C_Key || col == C_Text) {
                    Context x = ContextAt(ed, t, wb);
                    if (x.kind == X_Key && col == C_Key) {
                        doc.what = D_Param;
                        doc.name = Lower(d + wb, we - wb);
                        doc.ctrl = x.statedef ? "" : x.ctrl;
                    } else if (x.kind == X_Arg) {
                        doc.what = D_Arg;
                        doc.name = Lower(d + wb, we - wb);
                        doc.ctrl = Lower(kGenTriggers[x.trig].name, strlen(kGenTriggers[x.trig].name));
                    }
                }
            }
        }
        if (word != ed->hover_word) {
            ed->hover_word = word;
            ed->hover_time = 0.0f;
            ed->hover_text.clear();
            if (word != kNone && ed->hover_note) {
                ed->hover_text = ed->hover_note(word, word_end);
            }
        }
        ed->hover_time += io.DeltaTime;
        bool waited = ed->hover_time >= 0.35f;
        bool show_doc = waited && DocShows(doc);
        bool show_note = waited && !ed->hover_text.empty();
        if (on_error || show_doc || show_note) {
            ImGui::BeginTooltip();
            if (on_error) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(pal->error_mark), "%s", ed->error_msg.c_str());
                ImGui::PopTextWrapPos();
                if (show_doc || show_note) {
                    ImGui::Separator();
                }
            }
            if (show_doc) {
                ShowDoc(doc);
                if (show_note) {
                    ImGui::Separator();
                }
            }
            if (show_note) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                ImGui::TextUnformatted(ed->hover_text.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::EndTooltip();
        }
    } else {
        ed->hover_word = kNone;
        ed->hover_time = 0.0f;
    }
    ImGui::EndChild();

    if (ed->comp_open && active && !ed->comp_items.empty()) {
        DrawCompletion(ed);
    }
    ImGui::PopFont();
    if (ed->changed) {
        version++;
        ed->seen_version = version;
        ed->seen_size = text.size();
    }
    return ed->changed;
}

void Editor_GotoLine(CodeEditor *ed, int line, bool select_line, bool focus)
{
    ed->goto_line = std::max(1, line);
    ed->goto_select = select_line;
    ed->sel_begin = ed->sel_end = kNone;
    ed->want_focus = focus;
}

void Editor_Select(CodeEditor *ed, size_t begin, size_t end, bool focus)
{
    ed->sel_begin = begin;
    ed->sel_end = end;
    ed->goto_line = 0;
    ed->want_focus = focus;
}

bool Editor_Selection(const CodeEditor *ed, size_t *begin, size_t *end)
{
    if (ed->sel_begin != kNone) {
        *begin = std::min(ed->sel_begin, ed->sel_end);
        *end = std::max(ed->sel_begin, ed->sel_end);
    } else {
        *begin = SelBegin(ed);
        *end = SelEnd(ed);
    }
    return *begin != *end;
}

void Editor_Replace(CodeEditor *ed, std::string &text, unsigned int &version, size_t begin, size_t end,
                    const std::string &str)
{
    Sync(ed, text, version);
    begin = std::min(begin, text.size());
    end = std::max(begin, std::min(end, text.size()));
    ed->merge = false;
    Replace(ed, begin, end, str, G_None);
    ed->changed = false;
    version++;
    ed->seen_version = version;
    ed->seen_size = text.size();
}

void Editor_SetHighlights(CodeEditor *ed, std::vector<std::pair<size_t, size_t>> ranges, int current)
{
    ed->highlights = std::move(ranges);
    ed->highlight_cur = current;
}

size_t Editor_CtrlClicked(const CodeEditor *ed)
{
    return ed->ctrl_clicked;
}

int Editor_CursorLine(const CodeEditor *ed)
{
    if (ed->goto_line > 0) {
        return ed->goto_line;
    }
    if (ed->lines.empty()) {
        return 1;
    }
    return LineOf(ed, ed->sel_begin != kNone ? ed->sel_end : ed->cur) + 1;
}

size_t Editor_CursorOffset(const CodeEditor *ed)
{
    return ed->sel_begin != kNone ? ed->sel_end : ed->cur;
}

void Editor_SetErrorLine(CodeEditor *ed, int line, const char *message)
{
    ed->error_line = std::max(0, line);
    ed->error_msg = message != nullptr ? message : "";
}

void Editor_SetDecorations(CodeEditor *ed, std::function<int(int)> mark_of, std::function<ImU32(int)> background_of)
{
    ed->mark_of = std::move(mark_of);
    ed->background_of = std::move(background_of);
}

void Editor_SetNotes(CodeEditor *ed, std::function<std::string(int, ImU32 *)> note_of)
{
    ed->note_of = std::move(note_of);
}

int Editor_MarkClicked(const CodeEditor *ed)
{
    return ed->mark_clicked;
}

void Editor_SetZss(CodeEditor *ed, bool zss)
{
    ed->zss = zss;
}

void Editor_SetHoverNote(CodeEditor *ed, std::function<std::string(size_t, size_t)> note_of)
{
    ed->hover_note = std::move(note_of);
}

bool Editor_IsFocused(const CodeEditor *ed)
{
    return ed->focused && ed->id != 0 && GImGui != nullptr && GImGui->ActiveId == ed->id;
}

} // namespace dbg
