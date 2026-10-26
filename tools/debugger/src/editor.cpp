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

enum { K_Trigger, K_Redirect, K_Controller, K_Param, K_Keyword };

struct NameInfo {
    int trigger = -1;       /* in kTriggers */
    int redirect = -1;      /* in kRedirects */
    int controller = -1;    /* in kControllers */
    bool keyword = false;
    bool listed = false;    /* in Names.words */
};

struct Word {
    const char *name;       /* the known spelling */
    std::string lower;
    int kind;
};

struct Names {
    std::unordered_map<std::string, NameInfo> map;  /* by lower-case name */
    std::vector<Word> words;                        /* for completion, one of each, sorted */
};

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
        names.words.push_back(Word{ name, low, kind });
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
    for (int i = 0; i < IM_ARRAYSIZE(kParams); i++) {
        AddName(names, kParams[i], K_Param, i);
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
    return ni.trigger >= 0 || ni.redirect >= 0 || ni.controller >= 0;
}

/* ------------------------------------------------------------------ colours */

enum {
    C_Text, C_Comment, C_Header, C_Key, C_Op, C_Number, C_String, C_Trigger, C_Redirect, C_Controller, C_Keyword,
    C_Count
};

static const ImU32 kColors[C_Count] = {
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
    IM_COL32(197, 134, 192, 255),   /* loopstart */
};
static const ImU32 kBackground = IM_COL32(28, 28, 32, 255);
static const ImU32 kGutterBg = IM_COL32(34, 34, 40, 255);
static const ImU32 kLineNumber = IM_COL32(110, 110, 125, 255);
static const ImU32 kLineNumberCur = IM_COL32(200, 200, 210, 255);
static const ImU32 kCurrentLine = IM_COL32(255, 255, 255, 14);
static const ImU32 kSelection = IM_COL32(38, 79, 120, 255);
static const ImU32 kErrorBg = IM_COL32(150, 30, 30, 110);
static const ImU32 kErrorMark = IM_COL32(230, 60, 60, 255);
static const ImU32 kCursor = IM_COL32(240, 240, 240, 255);

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
                    col = ni->controller >= 0 ? C_Controller : C_Text;
                } else if (ni->trigger >= 0) {
                    col = C_Trigger;
                } else if (ni->redirect >= 0) {
                    col = C_Redirect;
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

/* The colour of each byte of a line. */
static void Highlight(const char *s, size_t n, std::vector<unsigned char> &colors)
{
    colors.assign(n + 1, (unsigned char)C_Text);
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
    bool comp_type = false;     /* the word is the value of "type =" */
    std::vector<int> comp_items;    /* in KnownNames().words */
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

    size_t hover_word = kNone;  /* the word under the mouse, and for how long */
    float hover_time = 0.0f;

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
    int a, b;
    SelLines(ed, &a, &b);
    bool all = true, any = false;
    for (int l = a; l <= b; l++) {
        size_t p = LineIndentEnd(ed, t, l);
        if (p < LineEnd(ed, t, l)) {
            any = true;
            if (t[p] != ';') {
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
            if (ind < le && t[ind] == ';') {
                out.append(t, lb, ind - lb);
                out.append(t, ind + 1, le - ind - 1);
                delta = cur_col > ind - lb ? -1 : 0;
            } else {
                out.append(t, lb, le - lb);
            }
        } else {
            if (ind < LineEnd(ed, t, l) || a == b) {
                out += ';';
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

/* ------------------------------------------------------------------ completion */

/* The names that match word: those that start with it first, then those that contain it. */
static void FindCompletions(const std::string &word, bool controllers_only, std::vector<int> &items)
{
    items.clear();
    const Names &names = KnownNames();
    std::string lw = Lower(word.data(), word.size());
    std::vector<int> inside;
    for (int i = 0; i < (int)names.words.size(); i++) {
        const Word &w = names.words[i];
        if (controllers_only) {
            auto it = names.map.find(w.lower);
            if (it == names.map.end() || it->second.controller < 0) {
                continue;
            }
        }
        size_t at = w.lower.find(lw);
        if (at == 0) {
            items.push_back(i);
        } else if (at != std::string::npos) {
            inside.push_back(i);
        }
    }
    items.insert(items.end(), inside.begin(), inside.end());
    if (items.size() > 50) {
        items.resize(50);
    }
}

/* Opens (or updates) the completion of the word before the cursor; closes it when there is none. */
static void UpdateCompletion(CodeEditor *ed, bool open)
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
    if (ed->cur - wb < 2 || !(IsAlpha(t[wb]) || t[wb] == '_')) {
        CloseCompletion(ed);
        return;
    }
    /* not in comments, strings or group headers */
    const char *s = t.data() + lb;
    size_t n = LineEnd(ed, t, l) - lb;
    Highlight(s, n, ed->colors);
    int col = ed->colors[wb - lb];
    if (col == C_Comment || col == C_String || col == C_Header) {
        CloseCompletion(ed);
        return;
    }
    size_t eq = FindEquals(s, 0, CommentStart(s, n));
    ed->comp_type = eq < wb - lb && IsTypeKey(s, 0, eq);
    std::string word = t.substr(wb, ed->cur - wb);
    FindCompletions(word, ed->comp_type, ed->comp_items);
    const Names &names = KnownNames();
    if (ed->comp_items.empty() || (ed->comp_items.size() == 1 && word == names.words[ed->comp_items[0]].name)) {
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
    const Word &w = KnownNames().words[ed->comp_items[ed->comp_sel]];
    return ed->text->compare(ed->comp_begin, ed->cur - ed->comp_begin, w.name) != 0;
}

static void AcceptCompletion(CodeEditor *ed)
{
    const Word &w = KnownNames().words[ed->comp_items[ed->comp_sel]];
    size_t begin = ed->comp_begin, end = ed->cur;
    CloseCompletion(ed);
    ed->anchor = ed->cur;
    Replace(ed, begin, end, w.name, G_None);
    ed->merge = false;
}

static ImVec4 Colour(int c)
{
    return ImGui::ColorConvertU32ToFloat4(kColors[c]);
}

/* The documentation of a name: a trigger (or redirection) unless controller_first and it is a controller. */
static void ShowDoc(const NameInfo &ni, bool controller_first)
{
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
    if (ni.controller >= 0 && (controller_first || (ni.trigger < 0 && ni.redirect < 0))) {
        const NameDoc &d = kControllers[ni.controller];
        ImGui::TextColored(Colour(C_Controller), "%s (state controller)", d.name);
        if (d.syntax[0] != '\0') {
            ImGui::TextColored(Colour(C_Key), "%s", d.syntax);
        }
        ImGui::TextUnformatted(d.desc);
    } else if (ni.trigger >= 0 || ni.redirect >= 0) {
        const NameDoc &d = ni.trigger >= 0 ? kTriggers[ni.trigger] : kRedirects[ni.redirect];
        ImGui::TextColored(Colour(ni.trigger >= 0 ? C_Trigger : C_Redirect), "%s", d.syntax);
        ImGui::TextUnformatted(d.desc);
    }
    ImGui::PopTextWrapPos();
}

static const char *KindName(const NameInfo &ni)
{
    if (ni.controller >= 0 && ni.redirect >= 0) {
        return "controller, redirection";
    }
    if (ni.controller >= 0) {
        return "controller";
    }
    if (ni.trigger >= 0) {
        return "trigger";
    }
    if (ni.redirect >= 0) {
        return "redirection";
    }
    if (ni.keyword) {
        return "keyword";
    }
    return "parameter";
}

/* The list of names, under the word; it takes no input (the keys stay with the editor). */
static void DrawCompletion(CodeEditor *ed)
{
    const Names &names = KnownNames();
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
            w = std::max(w, ImGui::CalcTextSize(names.words[ed->comp_items[i]].name).x);
        }
        w += ImGui::GetFontSize();
        for (int i = ed->comp_top; i < ed->comp_top + rows; i++) {
            const Word &word = names.words[ed->comp_items[i]];
            const NameInfo &ni = names.map.find(word.lower)->second;
            ImGui::PushID(i);
            ImGui::Selectable(word.name, i == ed->comp_sel, ImGuiSelectableFlags_None, ImVec2(w, 0.0f));
            ImGui::SameLine();
            ImGui::TextDisabled("%s", KindName(ni));
            ImGui::PopID();
        }
        if (count > rows) {
            ImGui::TextDisabled("%d of %d", ed->comp_sel + 1, count);
        }
        const NameInfo &sel = names.map.find(names.words[ed->comp_items[ed->comp_sel]].lower)->second;
        if (HasDoc(sel)) {
            ImGui::Separator();
            ShowDoc(sel, ed->comp_type);
        }
        ImGui::TextDisabled("Enter/Tab: take it, Esc: close");
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

    /* typed characters (AltGr is Ctrl+Alt) */
    bool typed = false;
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
                typed = true;
            }
        }
        io.InputQueueCharacters.resize(0);
    }
    if (typed) {
        UpdateCompletion(ed, true);
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
            m.font->RenderChar(dl, m.size, ImVec2(sx, y), kColors[colors[s - b]], (ImWchar)c);
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

    ImGui::SetNextWindowContentSize(ImVec2(content_w, content_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, kBackground);
    bool visible = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGuiWindow *win = g.CurrentWindow;
    ed->id = win->GetID("##text");
    ImGuiID wid = ed->id;
    ImRect area = win->InnerClipRect;
    if (!visible || area.GetWidth() <= 0.0f || area.GetHeight() <= 0.0f) {
        ImGui::EndChild();
        return false;
    }
    float text_w = area.GetWidth() - gutter - kMargin;
    float max_x = std::max(0.0f, content_w - win->InnerRect.GetWidth());
    float max_y = std::max(0.0f, content_h - win->InnerRect.GetHeight());

    ImGui::ItemAdd(area, wid);
    bool hovered = ImGui::ItemHoverable(area, wid, ImGuiItemFlags_None);

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
            dl->AddRectFilled(ImVec2(text_min.x, y), ImVec2(area.Max.x, y + m.line_h), kErrorBg);
        } else if (l == cur_line && active && sb == se) {
            dl->AddRectFilled(ImVec2(text_min.x, y), ImVec2(area.Max.x, y + m.line_h), kCurrentLine);
        }
        if (sb != se && sb <= le && se > lb) {
            size_t a = std::max(sb, lb), b = std::min(se, le);
            float xa = origin.x + TextX(m, d + lb, d + le, d + a);
            float xb = origin.x + TextX(m, d + lb, d + le, d + b);
            if (se > le) {
                xb += m.space * 0.5f;   /* the line end is selected too */
            }
            dl->AddRectFilled(ImVec2(xa, y), ImVec2(xb, y + m.line_h), kSelection);
        }
        Highlight(d + lb, le - lb, ed->colors);
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
                    dl->AddRectFilled(ImVec2(x - m.space, y), ImVec2(x + w + m.space, y + m.line_h), IM_COL32(28, 28, 32, 235));
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
            dl->AddRectFilled(ImVec2(floorf(cx), cy), ImVec2(floorf(cx) + 1.5f, cy + m.line_h), kCursor);
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
    dl->AddRectFilled(area.Min, ImVec2(area.Min.x + gutter, area.Max.y), kGutterBg);
    for (int l = first; l <= last; l++) {
        float y = origin.y + l * m.line_h;
        char num[16];
        int len = ImFormatString(num, sizeof(num), "%d", l + 1);
        float w = len * m.adv['0'];
        if (l + 1 == ed->error_line) {
            dl->AddRectFilled(ImVec2(area.Min.x, y), ImVec2(area.Min.x + 4.0f, y + m.line_h), kErrorMark);
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
        ImU32 col = l + 1 == ed->error_line ? kErrorMark : l == cur_line ? kLineNumberCur : kLineNumber;
        dl->AddText(ImVec2(area.Min.x + gutter - m.space - w, y + floorf((m.line_h - m.size) * 0.5f)), col, num,
                    num + len);
    }

    /* tooltips: the error of the line, the documentation of the name under the mouse */
    if (hovered && !ed->dragging && !io.MouseDown[0]) {
        ImVec2 mp = io.MousePos;
        int l = (int)floorf((mp.y - origin.y) / m.line_h);
        bool on_error = l >= 0 && l < count && l + 1 == ed->error_line && !ed->error_msg.empty();
        const NameInfo *doc = nullptr;
        bool doc_controller = false;
        size_t word = kNone;
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
                Highlight(d + lb, le - lb, ed->colors);
                int col = ed->colors[wb - lb];
                if (col == C_Trigger || col == C_Redirect || col == C_Controller) {
                    const NameInfo *ni = FindName(d + wb, we - wb);
                    if (ni != nullptr && HasDoc(*ni)) {
                        doc = ni;
                        doc_controller = col == C_Controller;
                        word = wb;
                    }
                }
            }
        }
        if (word != ed->hover_word) {
            ed->hover_word = word;
            ed->hover_time = 0.0f;
        }
        ed->hover_time += io.DeltaTime;
        bool show_doc = doc != nullptr && ed->hover_time >= 0.35f;
        if (on_error || show_doc) {
            ImGui::BeginTooltip();
            if (on_error) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 36.0f);
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", ed->error_msg.c_str());
                ImGui::PopTextWrapPos();
                if (show_doc) {
                    ImGui::Separator();
                }
            }
            if (show_doc) {
                ShowDoc(*doc, doc_controller);
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

void Editor_Select(CodeEditor *ed, size_t begin, size_t end)
{
    ed->sel_begin = begin;
    ed->sel_end = end;
    ed->goto_line = 0;
    ed->want_focus = true;
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

bool Editor_IsFocused(const CodeEditor *ed)
{
    return ed->focused && ed->id != 0 && GImGui != nullptr && GImGui->ActiveId == ed->id;
}

} // namespace dbg
