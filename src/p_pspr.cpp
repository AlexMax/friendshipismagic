
//**************************************************************************
//**
//** p_pspr.c : Heretic 2 : Raven Software, Corp.
//**
//** $RCSfile: p_pspr.c,v $
//** $Revision: 1.105 $
//** $Date: 96/01/06 03:23:35 $
//** $Author: bgokey $
//**
//**************************************************************************

// HEADER FILES ------------------------------------------------------------

#include <stdlib.h>

#include "doomdef.h"
#include "d_event.h"
#include "c_cvars.h"
#include "m_random.h"
#include "p_enemy.h"
#include "p_local.h"
#include "s_sound.h"
#include "doomstat.h"
#include "gi.h"
#include "p_pspr.h"
#include "templates.h"
#include "thingdef/thingdef.h"
#include "g_level.h"
#include "farchive.h"
#include "d_player.h"


// MACROS ------------------------------------------------------------------

#define LOWERSPEED				6.
#define RAISESPEED				6.

// TYPES -------------------------------------------------------------------

struct FGenericButtons
{
	int ReadyFlag;			// Flag passed to A_WeaponReady
	int StateFlag;			// Flag set in WeaponState
	int ButtonFlag;			// Button to press
	ENamedName StateName;	// Name of the button/state
};

enum EWRF_Options
{
	WRF_NoBob			= 1,
	WRF_NoSwitch		= 1 << 1,
	WRF_NoPrimary		= 1 << 2,
	WRF_NoSecondary		= 1 << 3,
	WRF_NoFire = WRF_NoPrimary | WRF_NoSecondary,
	WRF_AllowReload		= 1 << 4,
	WRF_AllowZoom		= 1 << 5,
	WRF_DisableSwitch	= 1 << 6,
	WRF_AllowUser1		= 1 << 7,
	WRF_AllowUser2		= 1 << 8,
	WRF_AllowUser3		= 1 << 9,
	WRF_AllowUser4		= 1 << 10,
};

// EXTERNAL FUNCTION PROTOTYPES --------------------------------------------

// PUBLIC FUNCTION PROTOTYPES ----------------------------------------------

// PRIVATE FUNCTION PROTOTYPES ---------------------------------------------

// EXTERNAL DATA DECLARATIONS ----------------------------------------------

// PUBLIC DATA DEFINITIONS -------------------------------------------------

// [SO] 1=Weapons states are all 1 tick
//		2=states with a function 1 tick, others 0 ticks.
CVAR(Int, sv_fastweapons, false, CVAR_SERVERINFO);

// PRIVATE DATA DEFINITIONS ------------------------------------------------

static FRandom pr_wpnreadysnd ("WpnReadySnd");
static FRandom pr_gunshot ("GunShot");

static const FGenericButtons ButtonChecks[] =
{
	{ WRF_AllowZoom,	WF_WEAPONZOOMOK,	BT_ZOOM,	NAME_Zoom },
	{ WRF_AllowReload,	WF_WEAPONRELOADOK,	BT_RELOAD,	NAME_Reload },
	{ WRF_AllowUser1,	WF_USER1OK,			BT_USER1,	NAME_User1 },
	{ WRF_AllowUser2,	WF_USER2OK,			BT_USER2,	NAME_User2 },
	{ WRF_AllowUser3,	WF_USER3OK,			BT_USER3,	NAME_User3 },
	{ WRF_AllowUser4,	WF_USER4OK,			BT_USER4,	NAME_User4 },
};

// CODE --------------------------------------------------------------------

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

IMPLEMENT_POINTY_CLASS(DPSprite)
	DECLARE_POINTER(Caller)
	DECLARE_POINTER(Next)
END_POINTERS

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

DPSprite::DPSprite(player_t *owner, AInventory *caller, int id)
: processPending(true), firstTic(true), Owner(owner), Caller(caller), ID(id)
{
	DPSprite *prev = nullptr;
	DPSprite *next = Owner->psprites;
	while (next != nullptr && next->ID < ID)
	{
		prev = next;
		next = next->Next;
	}
	Next = next;
	GC::WriteBarrier(this, next);
	if (prev == NULL)
	{
		Owner->psprites = this;
		GC::WriteBarrier(this);
	}
	else
	{
		prev->Next = this;
		GC::WriteBarrier(prev, this);
	}

	if (Next && Next->ID == ID && ID != 0)
		Next->Destroy(); // Replace it.
}

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

DPSprite *player_t::GetPSprite(psprnum_t layer)
{
	assert(layer > 0 && layer < NUMPSPRITES);

	DPSprite *weapon = nullptr;
	DPSprite *pspr = psprites;
	while (pspr)
	{
		if (pspr->ID == layer)
			return pspr;

		if (pspr->ID == ps_weapon)
			weapon = pspr;

		pspr = pspr->Next;
	}

	pspr = new DPSprite(this, ReadyWeapon, layer);

	if (layer == ps_flash && weapon)
	{
		pspr->x = weapon->x;
		pspr->y = weapon->y;
	}

	return pspr;
}

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

DPSprite *player_t::FindPSprite(int layer)
{
	if (layer == 0)
		return nullptr;

	DPSprite *pspr = psprites;
	while (pspr)
	{
		if (pspr->ID == layer)
			break;

		pspr = pspr->Next;
	}

	return pspr;
}

//---------------------------------------------------------------------------
//
// PROC P_NewPspriteTick
//
//---------------------------------------------------------------------------

void DPSprite::NewTick()
{
	// This function should be called after the beginning of a tick, before any possible
	// prprite-event, or near the end, after any possible psprite event.
	// Because data is reset for every tick (which it must be) this has no impact on savegames.
	for (int i = 0; i < MAXPLAYERS; i++)
	{
		if (playeringame[i])
		{
			DPSprite *pspr = players[i].psprites;
			while (pspr)
			{
				pspr->processPending = true;

				pspr = pspr->Next;
			}
		}
	}
}

//---------------------------------------------------------------------------
//
// PROC P_SetPsprite
//
//---------------------------------------------------------------------------

void DPSprite::SetState(FState *newstate, bool pending)
{
	if (ID == ps_weapon)
	{ // A_WeaponReady will re-set these as needed
		Owner->WeaponState &= ~(WF_WEAPONREADY | WF_WEAPONREADYALT | WF_WEAPONBOBBING | WF_WEAPONSWITCHOK | WF_WEAPONRELOADOK | WF_WEAPONZOOMOK |
								WF_USER1OK | WF_USER2OK | WF_USER3OK | WF_USER4OK);
	}

	// Special handling for the old hardcoded layers.
	if (ID > 0 && ID < NUMPSPRITES)
		Caller = Owner->ReadyWeapon;

	processPending = pending;

	do
	{
		if (newstate == nullptr)
		{ // Object removed itself.
			Destroy();
			return;
		}
		State = newstate;

		if (newstate->sprite != SPR_FIXED)
		{ // okay to change sprite and/or frame
			if (!newstate->GetSameFrame())
			{ // okay to change frame
				Frame = newstate->GetFrame();
			}
			if (newstate->sprite != SPR_NOCHANGE)
			{ // okay to change sprite
				Sprite = newstate->sprite;
			}
		}

		Tics = newstate->GetTics(); // could be 0

		if ((ID > 0 && ID < NUMPSPRITES) || Caller->IsKindOf(RUNTIME_CLASS(AWeapon)))
		{ // The targeter layers are affected by this too.
			if (sv_fastweapons == 2 && ID == ps_weapon)
				Tics = newstate->ActionFunc == nullptr ? 0 : 1;
			else if (sv_fastweapons == 3)
				Tics = (newstate->GetTics() != 0);
			else if (sv_fastweapons)
				Tics = 1;		// great for producing decals :)
		}

		if (newstate->GetMisc1())
		{ // Set coordinates.
			x = newstate->GetMisc1();
		}
		if (newstate->GetMisc2())
		{
			y = newstate->GetMisc2();
		}

		if (Owner->mo != nullptr)
		{
			FState *nextstate;
			if (newstate->CallAction(Owner->mo, Caller, &nextstate))
			{
				// It's possible this call resulted in this very layer being replaced.
				if (ObjectFlags & OF_EuthanizeMe)
				{
					return;
				}
				if (nextstate != nullptr)
				{
					newstate = nextstate;
					Tics = 0;
					continue;
				}
				if (State == nullptr)
				{
					Destroy();
					return;
				}
			}
		}

		newstate = State->GetNextState();
	} while (!Tics); // An initial state of 0 could cycle through.

	return;
}

void P_SetPsprite(player_t *player, psprnum_t id, FState *state, bool pending)
{
	if (player == nullptr) return;
	player->GetPSprite(id)->SetState(state, pending);
}

//---------------------------------------------------------------------------
//
// PROC P_BringUpWeapon
//
// Starts bringing the pending weapon up from the bottom of the screen.
// This is only called to start the rising, not throughout it.
//
//---------------------------------------------------------------------------

void P_BringUpWeapon (player_t *player)
{
	FState *newstate;
	AWeapon *weapon;
	DPSprite *psweapon = player->GetPSprite(ps_weapon);

	if (player->PendingWeapon == WP_NOCHANGE)
	{
		if (player->ReadyWeapon != nullptr)
		{
			psweapon->y = WEAPONTOP;
			psweapon->SetState(player->ReadyWeapon->GetReadyState());
		}
		return;
	}

	weapon = player->PendingWeapon;

	// If the player has a tome of power, use this weapon's powered up
	// version, if one is available.
	if (weapon != nullptr &&
		weapon->SisterWeapon &&
		weapon->SisterWeapon->WeaponFlags & WIF_POWERED_UP &&
		player->mo->FindInventory (RUNTIME_CLASS(APowerWeaponLevel2), true))
	{
		weapon = weapon->SisterWeapon;
	}

	if (weapon != nullptr)
	{
		if (weapon->UpSound)
		{
			S_Sound (player->mo, CHAN_WEAPON, weapon->UpSound, 1, ATTN_NORM);
		}
		newstate = weapon->GetUpState ();
		player->refire = 0;
	}
	else
	{
		newstate = nullptr;
	}
	player->PendingWeapon = WP_NOCHANGE;
	player->ReadyWeapon = weapon;
	psweapon->y = player->cheats & CF_INSTANTWEAPSWITCH
		? WEAPONTOP : WEAPONBOTTOM;
	psweapon->SetState(newstate);
	// make sure that the previous weapon's flash state is terminated.
	// When coming here from a weapon drop it may still be active.
	P_SetPsprite(player, ps_flash,  nullptr);
	player->mo->weaponspecial = 0;
}

//---------------------------------------------------------------------------
//
// PROC P_FireWeapon
//
//---------------------------------------------------------------------------

void P_FireWeapon (player_t *player, FState *state)
{
	AWeapon *weapon;

	// [SO] 9/2/02: People were able to do an awful lot of damage
	// when they were observers...
	if (player->Bot == nullptr && bot_observer)
	{
		return;
	}

	weapon = player->ReadyWeapon;
	if (weapon == nullptr || !weapon->CheckAmmo (AWeapon::PrimaryFire, true))
	{
		return;
	}

	player->mo->PlayAttacking ();
	weapon->bAltFire = false;
	if (state == nullptr)
	{
		state = weapon->GetAtkState(!!player->refire);
	}
	P_SetPsprite(player, ps_weapon, state);
	if (!(weapon->WeaponFlags & WIF_NOALERT))
	{
		P_NoiseAlert (player->mo, player->mo, false);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_FireWeaponAlt
//
//---------------------------------------------------------------------------

void P_FireWeaponAlt (player_t *player, FState *state)
{
	AWeapon *weapon;

	// [SO] 9/2/02: People were able to do an awful lot of damage
	// when they were observers...
	if (player->Bot == nullptr && bot_observer)
	{
		return;
	}

	weapon = player->ReadyWeapon;
	if (weapon == nullptr || weapon->FindState(NAME_AltFire) == nullptr || !weapon->CheckAmmo (AWeapon::AltFire, true))
	{
		return;
	}

	player->mo->PlayAttacking ();
	weapon->bAltFire = true;

	if (state == nullptr)
	{
		state = weapon->GetAltAtkState(!!player->refire);
	}

	P_SetPsprite(player, ps_weapon, state);
	if (!(weapon->WeaponFlags & WIF_NOALERT))
	{
		P_NoiseAlert (player->mo, player->mo, false);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_DropWeapon
//
// The player died, so put the weapon away.
//
//---------------------------------------------------------------------------

void P_DropWeapon (player_t *player)
{
	if (player == nullptr)
	{
		return;
	}
	// Since the weapon is dropping, stop blocking switching.
	player->WeaponState &= ~WF_DISABLESWITCH;
	if (player->ReadyWeapon != nullptr)
	{
		P_SetPsprite(player, ps_weapon, player->ReadyWeapon->GetDownState());
	}
}

//============================================================================
//
// P_BobWeapon
//
// [RH] Moved this out of A_WeaponReady so that the weapon can bob every
// tic and not just when A_WeaponReady is called. Not all weapons execute
// A_WeaponReady every tic, and it looks bad if they don't bob smoothly.
//
// [XA] Added new bob styles and exposed bob properties. Thanks, Ryan Cordell!
//
//============================================================================

void P_BobWeapon (player_t *player, float *x, float *y, double ticfrac)
{
	static float curbob;
	double xx[2], yy[2];

	AWeapon *weapon;
	float bobtarget;

	weapon = player->ReadyWeapon;

	if (weapon == nullptr || weapon->WeaponFlags & WIF_DONTBOB)
	{
		*x = *y = 0;
		return;
	}

	// [XA] Get the current weapon's bob properties.
	int bobstyle = weapon->BobStyle;
	float BobSpeed = (weapon->BobSpeed * 128);
	float Rangex = weapon->BobRangeX;
	float Rangey = weapon->BobRangeY;

	for (int i = 0; i < 2; i++)
	{
		// Bob the weapon based on movement speed.
		FAngle angle = (BobSpeed * 35 / TICRATE*(level.time - 1 + i)) * (360.f / 8192.f);

		// [RH] Smooth transitions between bobbing and not-bobbing frames.
		// This also fixes the bug where you can "stick" a weapon off-center by
		// shooting it when it's at the peak of its swing.
		bobtarget = float((player->WeaponState & WF_WEAPONBOBBING) ? player->bob : 0.);
		if (curbob != bobtarget)
		{
			if (fabsf(bobtarget - curbob) <= 1)
			{
				curbob = bobtarget;
			}
			else
			{
				float zoom = MAX(1.f, fabsf(curbob - bobtarget) / 40);
				if (curbob > bobtarget)
				{
					curbob -= zoom;
				}
				else
				{
					curbob += zoom;
				}
			}
		}

		if (curbob != 0)
		{
			float bobx = float(player->bob * Rangex);
			float boby = float(player->bob * Rangey);
			switch (bobstyle)
			{
			case AWeapon::BobNormal:
				xx[i] = bobx * angle.Cos();
				yy[i] = boby * fabsf(angle.Sin());
				break;

			case AWeapon::BobInverse:
				xx[i] = bobx*angle.Cos();
				yy[i] = boby * (1.f - fabsf(angle.Sin()));
				break;

			case AWeapon::BobAlpha:
				xx[i] = bobx * angle.Sin();
				yy[i] = boby * fabsf(angle.Sin());
				break;

			case AWeapon::BobInverseAlpha:
				xx[i] = bobx * angle.Sin();
				yy[i] = boby * (1.f - fabsf(angle.Sin()));
				break;

			case AWeapon::BobSmooth:
				xx[i] = bobx*angle.Cos();
				yy[i] = 0.5f * (boby * (1.f - ((angle * 2).Cos())));
				break;

			case AWeapon::BobInverseSmooth:
				xx[i] = bobx*angle.Cos();
				yy[i] = 0.5f * (boby * (1.f + ((angle * 2).Cos())));
			}
		}
		else
		{
			xx[i] = 0;
			yy[i] = 0;
		}
	}
	*x = (float)(xx[0] * (1. - ticfrac) + xx[1] * ticfrac);
	*y = (float)(yy[0] * (1. - ticfrac) + yy[1] * ticfrac);
}

//============================================================================
//
// PROC A_WeaponReady
//
// Readies a weapon for firing or bobbing with its three ancillary functions,
// DoReadyWeaponToSwitch(), DoReadyWeaponToFire() and DoReadyWeaponToBob().
// [XA] Added DoReadyWeaponToReload() and DoReadyWeaponToZoom()
//
//============================================================================

void DoReadyWeaponToSwitch (AActor *self, bool switchable)
{
	// Prepare for switching action.
	player_t *player;
	if (self && (player = self->player))
	{
		if (switchable)
		{
			player->WeaponState |= WF_WEAPONSWITCHOK | WF_REFIRESWITCHOK;
		}
		else
		{
			// WF_WEAPONSWITCHOK is automatically cleared every tic by P_SetPsprite().
			player->WeaponState &= ~WF_REFIRESWITCHOK;
		}
	}
}

void DoReadyWeaponDisableSwitch (AActor *self, INTBOOL disable)
{
	// Discard all switch attempts?
	player_t *player;
	if (self && (player = self->player))
	{
		if (disable)
		{
			player->WeaponState |= WF_DISABLESWITCH;
			player->WeaponState &= ~WF_REFIRESWITCHOK;
		}
		else
		{
			player->WeaponState &= ~WF_DISABLESWITCH;
		}
	}
}

void DoReadyWeaponToFire (AActor *self, bool prim, bool alt)
{
	player_t *player;
	AWeapon *weapon;

	if (!self || !(player = self->player) || !(weapon = player->ReadyWeapon))
	{
		return;
	}

	// Change player from attack state
	if (self->InStateSequence(self->state, self->MissileState) ||
		self->InStateSequence(self->state, self->MeleeState))
	{
		static_cast<APlayerPawn *>(self)->PlayIdle ();
	}

	// Play ready sound, if any.
	if (weapon->ReadySound && player->GetPSprite(ps_weapon)->GetState() == weapon->FindState(NAME_Ready))
	{
		if (!(weapon->WeaponFlags & WIF_READYSNDHALF) || pr_wpnreadysnd() < 128)
		{
			S_Sound (self, CHAN_WEAPON, weapon->ReadySound, 1, ATTN_NORM);
		}
	}

	// Prepare for firing action.
	player->WeaponState |= ((prim ? WF_WEAPONREADY : 0) | (alt ? WF_WEAPONREADYALT : 0));
	return;
}

void DoReadyWeaponToBob (AActor *self)
{
	if (self && self->player && self->player->ReadyWeapon)
	{
		// Prepare for bobbing action.
		self->player->WeaponState |= WF_WEAPONBOBBING;
		self->player->GetPSprite(ps_weapon)->x = 0;
		self->player->GetPSprite(ps_weapon)->y = WEAPONTOP;
	}
}

void DoReadyWeaponToGeneric(AActor *self, int paramflags)
{
	int flags = 0;

	for (size_t i = 0; i < countof(ButtonChecks); ++i)
	{
		if (paramflags & ButtonChecks[i].ReadyFlag)
		{
			flags |= ButtonChecks[i].StateFlag;
		}
	}
	if (self != NULL && self->player != NULL)
	{
		self->player->WeaponState |= flags;
	}
}

// This function replaces calls to A_WeaponReady in other codepointers.
void DoReadyWeapon(AActor *self)
{
	DoReadyWeaponToBob(self);
	DoReadyWeaponToFire(self);
	DoReadyWeaponToSwitch(self);
	DoReadyWeaponToGeneric(self, ~0);
}

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_WeaponReady)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_INT_OPT(flags)	{ flags = 0; }

													DoReadyWeaponToSwitch(self, !(flags & WRF_NoSwitch));
	if ((flags & WRF_NoFire) != WRF_NoFire)			DoReadyWeaponToFire(self, !(flags & WRF_NoPrimary), !(flags & WRF_NoSecondary));
	if (!(flags & WRF_NoBob))						DoReadyWeaponToBob(self);
													DoReadyWeaponToGeneric(self, flags);
	DoReadyWeaponDisableSwitch(self, flags & WRF_DisableSwitch);
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC P_CheckWeaponFire
//
// The player can fire the weapon.
// [RH] This was in A_WeaponReady before, but that only works well when the
// weapon's ready frames have a one tic delay.
//
//---------------------------------------------------------------------------

void P_CheckWeaponFire (player_t *player)
{
	AWeapon *weapon = player->ReadyWeapon;

	if (weapon == NULL)
		return;

	// Check for fire. Some weapons do not auto fire.
	if ((player->WeaponState & WF_WEAPONREADY) && (player->cmd.ucmd.buttons & BT_ATTACK))
	{
		if (!player->attackdown || !(weapon->WeaponFlags & WIF_NOAUTOFIRE))
		{
			player->attackdown = true;
			P_FireWeapon (player, NULL);
			return;
		}
	}
	else if ((player->WeaponState & WF_WEAPONREADYALT) && (player->cmd.ucmd.buttons & BT_ALTATTACK))
	{
		if (!player->attackdown || !(weapon->WeaponFlags & WIF_NOAUTOFIRE))
		{
			player->attackdown = true;
			P_FireWeaponAlt (player, NULL);
			return;
		}
	}
	else
	{
		player->attackdown = false;
	}
}

//---------------------------------------------------------------------------
//
// PROC P_CheckWeaponSwitch
//
// The player can change to another weapon at this time.
// [GZ] This was cut from P_CheckWeaponFire.
//
//---------------------------------------------------------------------------

void P_CheckWeaponSwitch (player_t *player)
{
	if (player == NULL)
	{
		return;
	}
	if ((player->WeaponState & WF_DISABLESWITCH) || // Weapon changing has been disabled.
		player->morphTics != 0)					// Morphed classes cannot change weapons.
	{ // ...so throw away any pending weapon requests.
		player->PendingWeapon = WP_NOCHANGE;
	}

	// Put the weapon away if the player has a pending weapon or has died, and
	// we're at a place in the state sequence where dropping the weapon is okay.
	if ((player->PendingWeapon != WP_NOCHANGE || player->health <= 0) &&
		player->WeaponState & WF_WEAPONSWITCHOK)
	{
		P_DropWeapon(player);
	}
}

//---------------------------------------------------------------------------
//
// PROC P_CheckWeaponButtons
//
// Check extra button presses for weapons.
//
//---------------------------------------------------------------------------

static void P_CheckWeaponButtons (player_t *player)
{
	if (player->Bot == nullptr && bot_observer)
	{
		return;
	}
	AWeapon *weapon = player->ReadyWeapon;
	if (weapon == nullptr)
	{
		return;
	}
	// The button checks are ordered by precedence. The first one to match a
	// button press and affect a state change wins.
	for (size_t i = 0; i < countof(ButtonChecks); ++i)
	{
		if ((player->WeaponState & ButtonChecks[i].StateFlag) &&
			(player->cmd.ucmd.buttons & ButtonChecks[i].ButtonFlag))
		{
			FState *state = weapon->GetStateForButtonName(ButtonChecks[i].StateName);
			// [XA] don't change state if still null, so if the modder
			// sets WRF_xxx to true but forgets to define the corresponding
			// state, the weapon won't disappear. ;)
			if (state != nullptr)
			{
				P_SetPsprite(player, ps_weapon, state);
				return;
			}
		}
	}
}

//---------------------------------------------------------------------------
//
// PROC A_ReFire
//
// The player can re-fire the weapon without lowering it entirely.
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_ReFire)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_STATE_OPT(state)	{ state = NULL; }
	A_ReFire(self, state);
	return 0;
}

void A_ReFire(AActor *self, FState *state)
{
	player_t *player = self->player;
	bool pending;

	if (NULL == player)
	{
		return;
	}
	pending = player->PendingWeapon != WP_NOCHANGE && (player->WeaponState & WF_REFIRESWITCHOK);
	if ((player->cmd.ucmd.buttons & BT_ATTACK)
		&& !player->ReadyWeapon->bAltFire && !pending && player->health > 0)
	{
		player->refire++;
		P_FireWeapon (player, state);
	}
	else if ((player->cmd.ucmd.buttons & BT_ALTATTACK)
		&& player->ReadyWeapon->bAltFire && !pending && player->health > 0)
	{
		player->refire++;
		P_FireWeaponAlt (player, state);
	}
	else
	{
		player->refire = 0;
		player->ReadyWeapon->CheckAmmo (player->ReadyWeapon->bAltFire
			? AWeapon::AltFire : AWeapon::PrimaryFire, true);
	}
}

DEFINE_ACTION_FUNCTION(AInventory, A_ClearReFire)
{
	PARAM_ACTION_PROLOGUE;
	player_t *player = self->player;

	if (NULL != player)
	{
		player->refire = 0;
	}
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC A_CheckReload
//
// Present in Doom, but unused. Also present in Strife, and actually used.
// This and what I call A_XBowReFire are actually the same thing in Strife,
// not two separate functions as I have them here.
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION(AInventory, A_CheckReload)
{
	PARAM_ACTION_PROLOGUE;

	if (self->player != NULL)
	{
		self->player->ReadyWeapon->CheckAmmo (
			self->player->ReadyWeapon->bAltFire ? AWeapon::AltFire
			: AWeapon::PrimaryFire, true);
	}
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC A_OverlayOffset
//
//---------------------------------------------------------------------------
enum WOFFlags
{
	WOF_KEEPX =		1,
	WOF_KEEPY =		1 << 1,
	WOF_ADD =		1 << 2,
};

void A_OverlayOffset(AActor *self, int layer, double wx, double wy, int flags)
{
	if ((flags & WOF_KEEPX) && (flags & WOF_KEEPY))
	{
		return;
	}

	player_t *player = self->player;
	DPSprite *psp;

	if (player && (player->playerstate != PST_DEAD))
	{
		psp = player->FindPSprite(layer);

		if (psp == nullptr)
			return;

		if (!(flags & WOF_KEEPX))
		{
			if (flags & WOF_ADD)
			{
				psp->x += wx;
			}
			else
			{
				psp->x = wx;
			}
		}
		if (!(flags & WOF_KEEPY))
		{
			if (flags & WOF_ADD)
			{
				psp->y += wy;
			}
			else
			{
				psp->y = wy;
			}
		}
	}
}

DEFINE_ACTION_FUNCTION(AInventory, A_OverlayOffset)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_INT_OPT(layer)	{ layer = ps_weapon; }
	PARAM_FLOAT_OPT(wx)		{ wx = 0.; }
	PARAM_FLOAT_OPT(wy)		{ wy = 32.; }
	PARAM_INT_OPT(flags)	{ flags = 0; }
	A_OverlayOffset(self, layer, wx, wy, flags);
	return 0;
}

DEFINE_ACTION_FUNCTION(AInventory, A_WeaponOffset)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_FLOAT_OPT(wx) { wx = 0.; }
	PARAM_FLOAT_OPT(wy) { wy = 32.; }
	PARAM_INT_OPT(flags) { flags = 0; }
	A_OverlayOffset(self, ps_weapon, wx, wy, flags);
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC A_Overlay
//
//---------------------------------------------------------------------------
DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_Overlay)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_INT(layer);
	PARAM_STATE_OPT(state)	{ state = nullptr; }

	player_t *player = self->player;

	if (player)
	{
		DPSprite *pspr;
		pspr = new DPSprite(player, reinterpret_cast<AInventory *>(stateowner), layer);
		pspr->SetState(state);
	}
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC A_Lower
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION(AInventory, A_Lower)
{
	PARAM_ACTION_PROLOGUE;

	player_t *player = self->player;
	DPSprite *psp;

	if (nullptr == player)
	{
		return 0;
	}
	psp = player->GetPSprite(ps_weapon);
	if (player->morphTics || player->cheats & CF_INSTANTWEAPSWITCH)
	{
		psp->y = WEAPONBOTTOM;
	}
	else
	{
		psp->y += LOWERSPEED;
	}
	if (psp->y < WEAPONBOTTOM)
	{ // Not lowered all the way yet
		return 0;
	}
	if (player->playerstate == PST_DEAD)
	{ // Player is dead, so don't bring up a pending weapon
		psp->y = WEAPONBOTTOM;
	
		// Player is dead, so keep the weapon off screen
		psp->SetState(nullptr);
		return 0;
	}
	// [RH] Clear the flash state. Only needed for Strife.
	P_SetPsprite(player, ps_flash,  nullptr);
	P_BringUpWeapon (player);
	return 0;
}

//---------------------------------------------------------------------------
//
// PROC A_Raise
//
//---------------------------------------------------------------------------

DEFINE_ACTION_FUNCTION(AInventory, A_Raise)
{
	PARAM_ACTION_PROLOGUE;

	if (self == nullptr)
	{
		return 0;
	}
	player_t *player = self->player;
	DPSprite *psp;

	if (nullptr == player)
	{
		return 0;
	}
	if (player->PendingWeapon != WP_NOCHANGE)
	{
		P_DropWeapon(player);
		return 0;
	}
	psp = player->GetPSprite(ps_weapon);
	psp->y -= RAISESPEED;
	if (psp->y > WEAPONTOP)
	{ // Not raised all the way yet
		return 0;
	}
	psp->y = WEAPONTOP;
	if (player->ReadyWeapon != nullptr)
	{
		psp->SetState(player->ReadyWeapon->GetReadyState());
	}
	else
	{
		psp->SetState(nullptr);
	}
	return 0;
}

//
// A_GunFlash
//
enum GF_Flags
{
	GFF_NOEXTCHANGE = 1,
};

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_GunFlash)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_STATE_OPT(flash)	{ flash = nullptr; }
	PARAM_INT_OPT  (flags)	{ flags = 0; }

	player_t *player = self->player;

	if (nullptr == player)
	{
		return 0;
	}
	if (!(flags & GFF_NOEXTCHANGE))
	{
		player->mo->PlayAttacking2 ();
	}
	if (flash == nullptr)
	{
		if (player->ReadyWeapon->bAltFire)
		{
			flash = player->ReadyWeapon->FindState(NAME_AltFlash);
		}
		if (flash == nullptr)
		{
			flash = player->ReadyWeapon->FindState(NAME_Flash);
		}
	}
	P_SetPsprite(player, ps_flash,  flash);
	return 0;
}



//
// WEAPON ATTACKS
//

//
// P_BulletSlope
// Sets a slope so a near miss is at aproximately
// the height of the intended target
//

DAngle P_BulletSlope (AActor *mo, FTranslatedLineTarget *pLineTarget, int aimflags)
{
	static const double angdiff[3] = { -5.625f, 5.625f, 0 };
	int i;
	DAngle an;
	DAngle pitch;
	FTranslatedLineTarget scratch;

	if (pLineTarget == NULL) pLineTarget = &scratch;
	// see which target is to be aimed at
	i = 2;
	do
	{
		an = mo->Angles.Yaw + angdiff[i];
		pitch = P_AimLineAttack (mo, an, 16.*64, pLineTarget, 0., aimflags);

		if (mo->player != NULL &&
			level.IsFreelookAllowed() &&
			mo->player->userinfo.GetAimDist() <= 0.5)
		{
			break;
		}
	} while (pLineTarget->linetarget == NULL && --i >= 0);

	return pitch;
}


//
// P_GunShot
//
void P_GunShot (AActor *mo, bool accurate, PClassActor *pufftype, DAngle pitch)
{
	DAngle 	angle;
	int 		damage;
		
	damage = 5*(pr_gunshot()%3+1);
	angle = mo->Angles.Yaw;

	if (!accurate)
	{
		angle += pr_gunshot.Random2 () * (5.625 / 256);
	}

	P_LineAttack (mo, angle, PLAYERMISSILERANGE, pitch, damage, NAME_Hitscan, pufftype);
}

DEFINE_ACTION_FUNCTION(AInventory, A_Light0)
{
	PARAM_ACTION_PROLOGUE;

	if (self->player != NULL)
	{
		self->player->extralight = 0;
	}
	return 0;
}

DEFINE_ACTION_FUNCTION(AInventory, A_Light1)
{
	PARAM_ACTION_PROLOGUE;

	if (self->player != NULL)
	{
		self->player->extralight = 1;
	}
	return 0;
}

DEFINE_ACTION_FUNCTION(AInventory, A_Light2)
{
	PARAM_ACTION_PROLOGUE;

	if (self->player != NULL)
	{
		self->player->extralight = 2;
	}
	return 0;
}

DEFINE_ACTION_FUNCTION_PARAMS(AInventory, A_Light)
{
	PARAM_ACTION_PROLOGUE;
	PARAM_INT(light);

	if (self->player != NULL)
	{
		self->player->extralight = clamp<int>(light, -20, 20);
	}
	return 0;
}

//------------------------------------------------------------------------
//
// PROC P_SetupPsprites
//
// Called at start of level for each player
//
//------------------------------------------------------------------------

void P_SetupPsprites(player_t *player, bool startweaponup)
{
	// Remove all psprites
	player->DestroyPSprites();

	// Spawn the ready weapon
	player->PendingWeapon = !startweaponup ? player->ReadyWeapon : WP_NOCHANGE;
	P_BringUpWeapon (player);
}

//------------------------------------------------------------------------
//
// PROC P_MovePsprites
//
// Called every tic by player thinking routine
//
//------------------------------------------------------------------------

void player_t::TickPSprites()
{
	DPSprite *weapon = nullptr;
	DPSprite *flash = nullptr;
	bool noweapon = (ReadyWeapon == nullptr && (health > 0 || mo->DamageType != NAME_Fire));

	DPSprite *pspr = psprites;
	while (pspr)
	{
		// Destroy the psprite if it's from a weapon that isn't currently selected by the player
		// or if it's from an inventory item that the player no longer owns. 
		// (except for the old hardcoded layers)
		if (!(pspr->ID > 0 && pspr->ID < NUMPSPRITES) &&
			(pspr->Caller == nullptr ||
			(pspr->Caller->IsKindOf(RUNTIME_CLASS(AWeapon)) && pspr->Caller != pspr->Owner->ReadyWeapon) ||
			(pspr->Caller->Owner != pspr->Owner->mo)))
		{
			pspr->Destroy();
		}
		else if (!(pspr->ID > 0 && pspr->ID < NUMPSPRITES && noweapon))
		{
			pspr->Tick();
		}

		if (pspr->ID == ps_weapon)
			weapon = pspr;
		else if (pspr->ID == ps_flash)
			flash = pspr;

		pspr = pspr->Next;
	}

	if (noweapon)
	{
		if (weapon) weapon->SetState(nullptr);
		if (flash) flash->SetState(nullptr);
		if (PendingWeapon != WP_NOCHANGE)
			P_BringUpWeapon(this);
	}
	else
	{
		P_CheckWeaponSwitch(this);
		if (WeaponState & (WF_WEAPONREADY | WF_WEAPONREADYALT))
		{
			P_CheckWeaponFire(this);
		}
		// Check custom buttons
		P_CheckWeaponButtons(this);
	}
}

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

void DPSprite::Tick()
{
	oldx = x;
	oldy = y;

	if (processPending)
	{
		// drop tic count and possibly change state
		if (Tics >= 0)	// a -1 tic count never changes
		{
			Tics--;

			// [BC] Apply double firing speed.
			// This is applied to the targeter layers too.
			if (((ID > 0 && ID < NUMPSPRITES) || (Caller->IsKindOf(RUNTIME_CLASS(AWeapon)))) &&
				(Tics && Owner->cheats & CF_DOUBLEFIRINGSPEED))
				Tics--;

			if (!Tics)
				SetState(State->GetNextState());
		}
	}
}

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

void DPSprite::Serialize(FArchive &arc)
{
	Super::Serialize(arc);

	arc << Next << Caller << Owner
		<< State << Tics << Sprite << Frame
		<< ID << x << y << oldx << oldy << ax << ay;
}

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

void player_t::DestroyPSprites()
{
	DPSprite *pspr = psprites;
	psprites = nullptr;
	while (pspr)
	{
		DPSprite *next = pspr->Next;
		pspr->Next = nullptr;
		pspr->Destroy();
		pspr = next;
	}
}

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

void DPSprite::Destroy()
{
	// Do not crash if this gets called on partially initialized objects.
	if (Owner != nullptr && Owner->psprites != nullptr)
	{
		if (Owner->psprites != this)
		{
			DPSprite *prev = Owner->psprites;
			while (prev != nullptr && prev->Next != this)
				prev = prev->Next;

			if (prev != nullptr && prev->Next == this)
			{
				prev->Next = Next;
				GC::WriteBarrier(prev, Next);
			}
		}
		else
		{
			Owner->psprites = Next;
			GC::WriteBarrier(Next);
		}
	}
	Super::Destroy();
}

//------------------------------------------------------------------------
//
//
//
//------------------------------------------------------------------------

ADD_STAT(psprites)
{
	FString out;
	DPSprite *pspr;
	for (int i = 0; i < MAXPLAYERS; i++)
	{
		if (!playeringame[i])
			continue;

		out.AppendFormat("[psprites] player: %d | layers: ", i);

		pspr = players[i].psprites;
		while (pspr)
		{
			out.AppendFormat("%d, ", pspr->GetID());

			pspr = pspr->GetNext();
		}

		out.AppendFormat("\n");
	}

	return out;
}
