//=============================================================================
// Loader of Spellcross units definition file JEDNOTKY.DEF.
// Loads CZ or EN version from binary data input.
// Decoders binary to list of unit records.
// 
// This code is part of Spellcross Map Editor project.
// (c) 2021, Stanislav Maslan, s.maslan@seznam.cz
// Distributed under MIT license, https://opensource.org/licenses/MIT.
//=============================================================================
#undef _HAS_STD_BYTE
#define _HAS_STD_BYTE 0

#include "spell_units.h"
#include "spellcross.h"
#include "spell_map_event.h"
#include "map.h"
#include <sstream>
#include <vector>
#include <stdexcept>
#include <random>
#include <algorithm>

using namespace std;

// default resource content
SpellUnitRec::SpellUnitRec()
{	
	gr_base = NULL;
	gr_aux = NULL;
	projectile = NULL;
	pnm_light_hit = NULL;
	pnm_armored_hit = NULL;
	pnm_air_hit = NULL;
	pnm_light_shot = NULL;
	pnm_armored_shot = NULL;
	pnm_air_shot = NULL;
	action_button_glyph = NULL;

	sound_move = NULL;
	sound_report = NULL;
	sound_contact = NULL;
	sound_hit = NULL;
	sound_die = NULL;
	sound_attack_light = NULL;
	sound_attack_armor = NULL;
	sound_attack_air = NULL;

	type_id = 0;
}

SpellUnitRec::~SpellUnitRec()
{
	if(sound_move)
		delete sound_move;
	if(sound_report)
		delete sound_report;
	if(sound_contact)
		delete sound_contact;
	if(sound_hit)
		delete sound_hit;
	if(sound_die)
		delete sound_die;
	if(sound_attack_light)
		delete sound_attack_light;
	if(sound_attack_armor)
		delete sound_attack_armor;
	if(sound_attack_air)
		delete sound_attack_air;
	if(sound_action)
		delete sound_action;
}

// copy resource name string ignoring trailing '_'
int GetNameStr(char *name, uint8_t* data, int count)
{
	int trail = false;
	for(int k = 0; k < count; k++)
	{
		if(data[k] == '_')
			trail = true;
		if(!trail)
			*name++ = data[k];
		else
			*name++ = '\0';
	}
	return(0);
}

// decode units def file
SpellUnits::SpellUnits(uint8_t* data, int dlen, FSUarchive *fsu, SpellGraphics *graphics,SpellSounds *sounds,UnitBonuses* bonuses)
{
	int count;
	if (dlen % 206 == 0 && dlen % 207 != 0)
	{
		// CZE version
		is_eng = 0;
		count = dlen / 206;
	}
	else if(dlen % 206 != 0 && dlen % 207 == 0)
	{
		// ENG version
		is_eng = 1;
		count = dlen / 207;
	}
	else
	{
		// unknown
		throw runtime_error("Cannot identify game version of JEDNOTKY.DEF file with units definitions!");
	}
	
	// --- for each unit:
	for (int k = 0; k < count; k++)
	{
		uint8_t *rec = data;
		if (is_eng)
			data += 207;
		else
			data += 206;
		
		// new unit record
		SpellUnitRec *unit = new SpellUnitRec();

		// unit type ID
		unit->type_id = k;

#define rdu8(rdu8_r) ((int)((unsigned int)*(rdu8_r)))
#define rdu16(rdu16_r) ((int)((unsigned int)*((unsigned short*)(rdu16_r))))
#define rdu32(rdu32_r) ((int)*((unsigned int*)(rdu32_r)))
#define rdsc(rdsc_d,rdsc_r,rdsc_n) memcpy((void*)(rdsc_d),(void*)(rdsc_r),rdsc_n)

		// unit name
		rdsc(unit->name, rec + 0x00, 28);

		// unit info resource
		rdsc(unit->info, rec + 0x1C, 9);

		// unit graphics resource
		GetNameStr(unit->gra, rec + 0x25,6);

		// unit aditional graphics resource (tank turrets or so)
		GetNameStr(unit->grb,rec + 0x2B, 6);

		// unit icon
		rdsc(unit->icon, rec + 0x31, 9);

		// unit class flags
		// 0x01 - has turret
		// 0x02 - walk movement
		// 0x04 - flight movement
		// 0x08 - hover movement (can move on water)
		// 0x30 - {0-air, 1-light, 2-armored unit}
		// 0x40 - flesh and bones
		// 0x80 - only Demon
		unit->utype = rdu8(rec + 0x3A);

		// ap count
		unit->ap = rdu8(rec + 0x3B);

		// ap per move in forrest
		unit->apfw = rdu8(rec + 0x3C);

		// ap per move normal
		unit->apw = rdu8(rec + 0x3D);

		// man count (or tanks count)
		unit->cnt = rdu8(rec + 0x3E);

		// attack to light units
		unit->attack_light = rdu8(rec + 0x3F);

		// attack to armored units
		unit->attack_armored = rdu8(rec + 0x40);

		// attack to air units
		unit->attack_air = rdu8(rec + 0x41);

		// attack to objects
		unit->attack_objects = rdu8(rec + 0x42);

		// unit defence
		unit->defence = rdu8(rec + 0x43);

		// some probability???
		unit->res2 = rdu8(rec + 0x44);

		// base initiative (used by original defensive/opportunity fire)
		unit->res3 = rdu8(rec + 0x45);

		// max fire range
		unit->fire_range = rdu8(rec + 0x46);

		// special shot flags
		// 0x01 - high turret origin (probably???)
		// 0x02 - indirect fire (artilery)
		// 0x04 - steals action points
		// 0x08 - inefficient to armored targets (probably???)
		// 0x10 - can't fire from slopes
		// 0x20 - indirect missile (MLRS only???)
		// 0x40 - fire projectile
		// 0x80 - fire sensitivity (increased fire sensitivity???)
		unit->fire_flags = rdu8(rec + 0x47);		
		// aditional ENG version item
		// 0x100 - unit is healed by fire
		if (is_eng)
		{
			unit->fire_flags |= rdu8(rec + 0x48)<<8;
			rec++;
		}

		// ap per shot
		unit->aps = rdu8(rec + 0x48);


		// light attack hit animation (*.pnm animation)
		rdsc(unit->pnm_light_hit_name, rec + 0x49, 9);

		// light attack unit animation resource (*.fsu resource)
		GetNameStr(unit->anim_atack_light_name,rec + 0x52,6);
		
		// used frames count
		unit->anim_atack_light_frames = rdu8(rec + 0x61);

		// light attack shot animation (*.pnm resource)
		rdsc(unit->pnm_light_shot_name, rec + 0x58, 9);

		// armored attack hit animation (*.pnm animation)
		rdsc(unit->pnm_armored_hit_name, rec + 0x62, 9);

		// armored attack unit animation resource (*.fsu resource)
		GetNameStr(unit->anim_atack_armor_name,rec + 0x6B,6);
		// used frames count
		unit->anim_atack_armor_frames = rdu8(rec + 0x7A);

		// armored attack shot animation (*.pnm resource)
		rdsc(unit->pnm_armored_shot_name, rec + 0x71, 9);

		// air attack hit animation (*.pnm animation)
		rdsc(unit->pnm_air_hit_name, rec + 0x7B, 9);

		// air attack unit animation resource (*.fsu content)
		GetNameStr(unit->anim_atack_air_name,rec + 0x84,6);
		// used frames count
		unit->anim_atack_air_frames = rdu8(rec + 0x93);

		// air attack shot animation (*.pnm resource)
		rdsc(unit->pnm_air_shot_name, rec + 0x8A, 9);

		// projectile visibility flags (0x01-light, 0x02-armored, 0x04-air attacks)
		unit->projectile_visible = rdu8(rec + 0x94);

		// projectile resource (*.grf files)
		rdsc(unit->projetile_name, rec + 0x95, 13);

		// special action id
		// 1  - enable/disable radar (par3-radar indirect sight range)
		// 2  - show tank turret (UDES) (par3-unit to transform to)
		// 3  - hide tank turret (UDES) (par3-unit to transform to)
		// 4  - fire teleport movement (hell cavalery/demon)
		// 5  - create unit (par3-unit to create)
		// 6  - lower enemy morale (undead) (par1-range, par2-level, par3-range)
		// 7  - aircraft up (par3-unit to transform to)
		// 8  - aircraft land (par3-unit to transform to)
		// 9  - paralyze enemy (harpya) (par1-range, par2-???, par3-???)
		// 11 - freeze enemy units (par1-range, par2-???, par3-???)
		// 12 - dragons fear (par1-range, par2-???, par3-???)
		// 13 - autodestruction (par1-range, par2-intensity, par3-???)
		// 14 - breorns scream (par1-range, par2-???, par3-???)
		// 15 - kamize attack
		// 16 - transform to fortres (par3-unit to transform to)
		// 17 - transform from fortres (par3-unit to transfrom to)
		unit->action_id = rdu8(rec + 0xA2);

		// special action animation resource (*.fsu content)
		GetNameStr(unit->action_fsu_name,rec + 0xA4,6);
		// frames count
		unit->action_fsu_frames = rdu8(rec + 0xAA);

		// ap per special action
		unit->action_ap = rdu8(rec + 0xAB);

		// special action parameters
		unit->action_params[0] = rdu8(rec + 0xAC);
		unit->action_params[1] = rdu8(rec + 0xAD);
		unit->action_params[2] = rdu8(rec + 0xAE);

		// die action
		unit->die_action_id = rdu8(rec + 0xBB);

		// die action parameters
		unit->die_action_params[0] = rdu8(rec + 0xB9);
		unit->die_action_params[1] = rdu8(rec + 0xBA);
		unit->die_action_params[2] = rdu8(rec + 0xBD);

		// die action animation resource (*.fsu content)
		GetNameStr(unit->die_anim_name,rec + 0xB2,6);
		// frames count
		unit->die_anim_frames = rdu8(rec + 0xB8);


		// direct sight
		unit->sdir = rdu8(rec + 0xAF);

		// visible men/vehicles count
		unit->vis = rdu8(rec + 0xB0);

		// rounds per dig level
		unit->dig_turns = rdu8(rec + 0xB1);

		// unit movement sound class
		unit->smov = rdu8(rec + 0xBF);

		// unit light attack class
		unit->slig = rdu8(rec + 0xC0);

		// unit armored attack class
		unit->sarm = rdu8(rec + 0xC1);

		// unit air attack class
		unit->sair = rdu8(rec + 0xC2);

		// unit hit sound class
		unit->shit = rdu8(rec + 0xC3);

		// unit special sound class
		unit->snd_action_id = rdu8(rec + 0xC4);

		// unit selection/report sound class
		unit->ssel = rdu8(rec + 0xC5);

		// unit min rank points
		unit->exp_min = rdu32(rec + 0xC6);
		// unit max rank points x200
		unit->exp_max = rdu32(rec + 0xCA);
		// precalculate experience points limits
		for(int e = 0; e < 12; e++)
			unit->exp_limits[e] = unit->CalcExperiencePts(e);

		// --- assign FSU resources:
		if(fsu)
		{
			unit->gr_base = fsu->GetResource(unit->gra);
			unit->gr_aux = fsu->GetResource(unit->grb);
			unit->gr_attack_light = fsu->GetResource(unit->anim_atack_light_name);
			unit->gr_attack_armor = fsu->GetResource(unit->anim_atack_armor_name);
			unit->gr_attack_air = fsu->GetResource(unit->anim_atack_air_name);
			unit->gr_action = fsu->GetResource(unit->action_fsu_name);
			unit->gr_die = fsu->GetResource(unit->die_anim_name);
		}

		// --- assign aux graphics
		if(graphics)
		{
			// find unit icon
			unit->icon_glyph = graphics->GetResource(unit->icon);

			// find unit projectile
			unit->projectile = graphics->GetProjectile(unit->projetile_name);

			// find shot/hit PNMs
			unit->pnm_air_hit = graphics->GetPNM(unit->pnm_air_hit_name);
			unit->pnm_air_shot = graphics->GetPNM(unit->pnm_air_shot_name);
			unit->pnm_light_hit = graphics->GetPNM(unit->pnm_light_hit_name);
			unit->pnm_light_shot = graphics->GetPNM(unit->pnm_light_shot_name);
			unit->pnm_armored_hit = graphics->GetPNM(unit->pnm_armored_hit_name);
			unit->pnm_armored_shot = graphics->GetPNM(unit->pnm_armored_shot_name);

			// assign special action button glyph
			if(unit->isActionTurretUp())
				unit->action_button_glyph = graphics->wm_glyph_up;
			else if(unit->isActionTurretDown())
				unit->action_button_glyph = graphics->wm_glyph_down;
			else if(unit->isActionToggleRadar())
			{
				unit->action_button_glyph = graphics->wm_glyph_radar_up;
				unit->action_button_glyph_b = graphics->wm_glyph_radar_down;
			}
			else if(unit->isActionTakeOff())
				unit->action_button_glyph = graphics->wm_glyph_up;
			else if(unit->isActionLand())
				unit->action_button_glyph = graphics->wm_glyph_down;
			else if(unit->isActionToFortres())
				unit->action_button_glyph = graphics->wm_glyph_down;
			else if(unit->isActionFromFortres())
				unit->action_button_glyph = graphics->wm_glyph_up;
			else if(unit->isActionCreateUnit())
				unit->action_button_glyph = graphics->wm_glyph_place_unit;


		}

		// --- assign sounds
		if(sounds)
		{
			unit->sound_move = sounds->GetMoveClass(unit->smov);
			unit->sound_report = sounds->GetReportClass(unit->ssel);
			unit->sound_contact = sounds->GetContactClass(unit->ssel);
			unit->sound_hit = sounds->GetHitClass(unit->shit);
			unit->sound_die = sounds->GetDieClass(unit->shit);
			unit->sound_attack_light = sounds->GetAttackClass(unit->slig);
			unit->sound_attack_armor = sounds->GetAttackClass(unit->sarm);
			unit->sound_attack_air = sounds->GetAttackClass(unit->sair);			
			unit->sound_action = sounds->GetSpecialClass(unit->snd_action_id);
			unit->sound_level_up = sounds->aux_samples.unit_level_up;
		}

		// store BONUSES.DEF link
		unit->bonuses = bonuses;

		// store unit to list
		units.push_back(unit);
	}

}

// unit type
int SpellUnitRec::isAir()
{
	return((utype & UTYPE_TYPE_MASK) == UTYPE_TYPE_AIR);
}
int SpellUnitRec::isLight()
{
	return((utype & UTYPE_TYPE_MASK) == UTYPE_TYPE_LIGHT);
}
int SpellUnitRec::isArmored()
{
	return((utype & UTYPE_TYPE_MASK) == UTYPE_TYPE_ARMORED);
}
int SpellUnitRec::isLand()
{
	return(isLight() || isArmored());
}
int SpellUnitRec::hasTurret()
{
	return(!!(utype & UTYPE_TURRET));
}
int SpellUnitRec::isWalk()
{
	return(!!(utype & UTYPE_WALK));
}
int SpellUnitRec::isFly()
{
	return(!!(utype & UTYPE_FLY));
}
int SpellUnitRec::isHover()
{
	return(!!(utype & UTYPE_HOVER));
}
int SpellUnitRec::isTank()
{
	return(!isWalk() && !isFly());
}
int SpellUnitRec::isMLRS()
{
	return(!!(fire_flags & FIRE_MLRS));
}
int SpellUnitRec::isFlashAndBones()
{
	return(!!(utype & UTYPE_FLAHS));
}
int SpellUnitRec::isMetal()
{
	// ###todo: maybe not correct?
	return(!isFlashAndBones());
}
int SpellUnitRec::isInefficientToArmor()
{
	return(!!(fire_flags & FIRE_INEFFICIENT_TO_ARMORRED));
}
int SpellUnitRec::stealsActionPoints()
{
	return(!!(fire_flags & FIRE_STEAL_AP));
}
int SpellUnitRec::isFireSensitive()
{
	return(!!(fire_flags & FIRE_FIRE_SENSITIVE));
}
int SpellUnitRec::isFireHealed()
{
	return(!!(fire_flags & FIRE_FIRE_HEALED));
}
int SpellUnitRec::hasFireAttack()
{
	return(!!(fire_flags & FIRE_FIRE_PROJECTILE));
}
int SpellUnitRec::isSingleMan()
{
	return(cnt == 1);
}
// calculate base experience points for given exp. level 1-12 (use for precalculation only)
int SpellUnitRec::CalcExperiencePts(int level)
{
	// very crude approximation of strange Spellcross experience boundaries
	// note: it's not accurate, but decently close...
	double a = (double)exp_min;
	double b = log(200.0*exp_max/exp_min)/log(12);
	level = (std::min)((std::max)(level,0),11);
	int points = (int)(a*pow((double)level,b));
	return(points);
}
// get base experience points for given exp. level 1-12
int SpellUnitRec::GetExperiencePts(int level)
{
	return(exp_limits[(std::min)((std::max)(level-1,0),11)]);
}
// get base experience points for next of given exp. level 1-12
int SpellUnitRec::GetNextExperiencePts(int level)
{
	return(exp_limits[(std::min)((std::max)(level,0),11)]);
}

// uses projectile when shooting to target unit (or NULL to object)?
int SpellUnitRec::hasProjectile(SpellUnitRec* target)
{
	int has = false;
	if(target)
	{
		has = target->isLight() && (projectile_visible & PROJECTILE_LIGHT) || 
		target->isArmored() && (projectile_visible & PROJECTILE_ARMOR) ||
		target->isAir() && (projectile_visible & PROJECTILE_AIR);
	}
	else
	{
		has = projectile_visible & (PROJECTILE_ARMOR | PROJECTILE_LIGHT | PROJECTILE_AIR);
	}
	return(has);
}
// uses teleport move (demon, hell cavallery)?
int SpellUnitRec::usingTeleportMove()
{
	return(action_id == SPEC_ACT_FIRE_TELEPORT);
}
// has unit special action? (excluding teleport move), returns non-zero action ID if exist
int SpellUnitRec::hasSpecAction()
{
	if(action_id == SPEC_ACT_FIRE_TELEPORT)
		return(0);
	return(action_id);
}
int SpellUnitRec::isActionTurretUp()
{
	return(action_id == SPEC_ACT_SHOW_TURRET);
}
int SpellUnitRec::isActionTurretDown()
{
	return(action_id == SPEC_ACT_HIDE_TURRET);
}
int SpellUnitRec::isActionToggleRadar()
{
	return(action_id == SPEC_ACT_TOGGLE_RADAR);
}
int SpellUnitRec::isActionLand()
{
	return(action_id == SPEC_ACT_AIRCRAFT_DOWN);
}
int SpellUnitRec::isActionTakeOff()
{
	return(action_id == SPEC_ACT_AIRCRAFT_UP);
}
int SpellUnitRec::isActionToFortres()
{
	return(action_id == SPEC_ACT_TRANSFORM_TO_FORTRES);
}
int SpellUnitRec::isActionFromFortres()
{
	return(action_id == SPEC_ACT_TRANSFORM_FROM_FORTRES);
}
int SpellUnitRec::isActionCreateUnit()
{
	return(action_id == SPEC_ACT_CREATE_UNIT);
}
int SpellUnitRec::isActionKamikaze()
{
	return(action_id == SPEC_ACT_KAMIZAZE);
}

SpellGraphicItem *SpellUnitRec::GetActionBtnGlyph(int alt)
{
	if(alt)
		return(action_button_glyph_b);
	return(action_button_glyph);
}

// can unit attack target?
int SpellUnitRec::canAttack(SpellUnitRec* target)
{
	if(!target)
		return(false);

	// special attackers that only apply morale/fear/paralyze should still be able to "attack"
	if(action_id == SPEC_ACT_LOWER_MORALE ||
		action_id == SPEC_ACT_DRAGON_FEAR ||
		action_id == SPEC_ACT_PARALYZE)
		return(true);

	if(target->isLight() && !attack_light)
		return(false);
	if(target->isArmored() && !attack_armored)
		return(false);
	if(target->isAir() && !attack_air)
		return(false);
	return(true);
}
int SpellUnitRec::canAttackObject()
{	
	return(attack_objects > 0);
}
// get maximum dig level
int SpellUnitRec::GetMaxDig()
{
	if(isAir())
		return(0);
	if(!dig_turns)
		return(0);
	if(isLight())
		return(6);
	else
		return(2);
}




// cleanup
SpellUnits::~SpellUnits()
{
	for (int k = 0; k < units.size(); k++)
		delete units[k];
	units.clear();
}

// get units count
int SpellUnits::Count()
{
	return(units.size());
}

// get unit record by order id
SpellUnitRec *SpellUnits::GetUnit(int uid)
{
	if (uid >= units.size())
		return(NULL);
	return(units[uid]);
}

// get full units list
vector<SpellUnitRec*> &SpellUnits::GetUnits()
{
	return(units);
}


// render unit (complete, i.e. group of man or tank with turret) and stick for air units:
//   frame - animation frame if applicable
//   *fsu_anim - animation override
//   flight_alt - air unit flight altitute in percent
tuple<int,int> SpellUnitRec::Render(uint8_t* buffer, uint8_t* buf_end, int buf_x_pos, int buf_y_pos, int buf_x_size,
	uint8_t* filter, uint8_t* shadow_filter, ::Sprite *sprt, int man, int azim, int azim_turret, int frame, FSU_resource *fsu_anim, int flight_alt)
{
	// tile slope
	char slope = sprt->name[2];
	
	// visible man count in unit
	//int man = vis;
	if (man < 1)
		man = 1; // overrider for tanks
	if (man > 5)
		man = 5; // limit units to 4 (###todo: may be extended later)

	// buffer of man positions relatie to origin
	double uofs_x[5];
	double uofs_y[5];
	int uofs[5];

	// precalculate sin/cos to save time
	static int isa = 0;
	static double cosa[360];
	static double sina[360];
	for (isa = 0; isa < 360; isa++)
	{
		cosa[isa] = cos(isa / 180.0 * 3.1415);
		sina[isa] = sin(isa / 180.0 * 3.1415);
	}

	int man_id = 0;
	if (man == 1 || man == 5)
	{
		// one unit, place to center
		uofs_x[man_id] = 0;
		uofs_y[man_id] = 0;
		uofs[man_id] = 0;
		man_id++;
	}
	if (man >= 2 && man <= 5)
	{
		// 2-5 units - make ring around center
		int ang_step;
		int ang_ofs;		
		if (man == 2)
		{
			ang_ofs = 315;
			ang_step = 180;
		}
		else if (man == 3)
		{
			ang_ofs = 30;
			ang_step = 120;
		}
		else
		{
			ang_ofs = 1;
			ang_step = 90;
		}
		for (int k = 0; k < (std::min)(man,4); k++)
		{
			// calculate man placement
			uofs_x[man_id] = sina[ang_ofs];
			uofs_y[man_id] = cosa[ang_ofs];
			uofs[man_id] = 0;
			man_id++;
			// next angle
			ang_ofs += ang_step;
			if (ang_ofs >= 360)
				ang_ofs -= 360;
		}
	}
	/*else
	{
		int ang_step = 360/man;
		int ang_ofs = 0;
		for (int k = 0; k < man; k++)
		{
			// calculate man placement
			uofs_x[man_id] = sina[ang_ofs];
			uofs_y[man_id] = cosa[ang_ofs];
			uofs[man_id] = 0;
			man_id++;
			// next angle
			ang_ofs += ang_step;
			if (ang_ofs >= 360)
				ang_ofs -= 360;
		}
	}*/

	int x_pos;
	int y_pos;
	FSU_sprite* spr = NULL;

	// NOTE: use a large positive sentinel; `2^30` would be XOR (== 28)
	int y_status_bar = (1 << 30);
	int x_status_bar = 0;

	// flight altitute (in pixels)
	flight_alt = (flight_alt>=0)?(AIR_UNIT_FLY_HEIGHT*flight_alt/100):AIR_UNIT_FLY_HEIGHT;

	// --- repeat for each man:
	for (int uid = 0; uid < man; uid++)
	{
		// search maximum y-offset because we render from back to front
		double min_y = 10000.0;
		for (int k = 0; k < man; k++)
		{
			if (!uofs[k] && uofs_y[k] < min_y)
			{
				min_y = uofs_y[k];
				man_id = k;
			}
		}
		uofs[man_id] = 1;
		
		int was_anim = false;
		for (int part = 0; part < 2; part++)
		{
			FSU_resource* res;
			int res_azim;
			if (part == 0)
			{
				res = gr_base;
				res_azim = azim;
			}
			else
			{
				res = gr_aux;
				res_azim = azim_turret;
			}
			if (!res)
				break;
			
			int y_org = 0;
			if (res->stat.slopes == 1)
				slope = 'A';
												
			// get sprite record (SAFE)
			spr = NULL;
			was_anim = false;

			// normalize indices
			int slope_idx = slope - 'A';
			if (slope_idx < 0) slope_idx = 0;

			if (fsu_anim && frame >= 0 && frame < fsu_anim->anim.frames)
			{
				// custom animation resource
				if (fsu_anim->anim.azimuths > 0)
				{
					int a = res_azim;
					if (a < 0) a = 0;
					a %= fsu_anim->anim.azimuths;

					if (fsu_anim->anim.lists && fsu_anim->anim.lists[a])
						spr = fsu_anim->anim.lists[a][frame];
				}
				else
				{
					if (fsu_anim->anim.slopes > 0 && slope_idx >= fsu_anim->anim.slopes)
						slope_idx = fsu_anim->anim.slopes - 1;

					if (fsu_anim->anim.lists && fsu_anim->anim.lists[slope_idx])
						spr = fsu_anim->anim.lists[slope_idx][frame];
				}

				was_anim = (spr != NULL);
			}
			else if (frame >= 0 && res->anim.frames > 0)
			{
				// normal animation (azimuth-based)
				if (res->anim.azimuths > 0)
				{
					int a = res_azim;
					if (a < 0) a = 0;
					a %= res->anim.azimuths;

					if (res->anim.lists && res->anim.lists[a])
						spr = res->anim.lists[a][frame];
				}

				was_anim = (spr != NULL);
			}

			// fallback to static if animation sprite missing
			if (!spr)
			{
				if (res->stat.slopes > 0 && slope_idx >= res->stat.slopes)
					slope_idx = res->stat.slopes - 1;

				if (res->stat.azimuths > 0)
				{
					int a = res_azim;
					if (a < 0) a = 0;
					a %= res->stat.azimuths;

					spr = res->stat.lists[slope_idx][a];
				}
			}


			// unit position
			if (isWalk())
			{
				// for walking units we have to perform placement transform based on terrain for each man
				x_pos = buf_x_pos + (int)(uofs_x[man_id] * MAN_RING_DIAMETER);
				y_pos = buf_y_pos - (int)(sprt->GetTileProjY(uofs_x[man_id] * MAN_RING_DIAMETER, -uofs_y[man_id] * MAN_RING_DIAMETER));
			}
			else
			{
				// for all other units do nothing, place it as prescribed by unit sprite itself
				x_pos = buf_x_pos;
				y_pos = buf_y_pos + sprt->y_ofs;
			}
			
			if (isAir())
			{
				// air unit - shift unit up
				y_pos -= flight_alt;
			}

			// store unit highest pixel offset
			if(uid == 0)
				x_status_bar = 40;
			y_status_bar = (std::min)(y_pos + (spr->y_ofs - 128) - buf_y_pos,y_status_bar);

			// render man of unit			
			spr->Render(buffer, buf_end, x_pos, y_pos, buf_x_size, shadow_filter, filter);

			// this should prevent rendering turret when animating
			if(was_anim)
				break;
		}
	}

	// --- render "stick" to ground:
	if (spr && isAir() && man == 1 && !gr_aux)
	{
		// only for air, single man unit, not tank

		// top of the stick
		x_pos = buf_x_pos + 40;
		y_pos = buf_y_pos - flight_alt + spr->y_ofs - 128 + spr->y_size + sprt->y_ofs;
		if (y_pos < 0)
			y_pos = 0;

		// bottom of the stick
		int y_down = buf_y_pos + sprt->y_size / 2 + sprt->y_ofs;
		if (y_down > (buf_end - buffer) / buf_x_size)
			y_down = (buf_end - buffer) / buf_x_size;

		// render stick
		for (int y = y_down; y >= y_pos; y-=3)
			buffer[x_pos + y * buf_x_size] = 250;

	}

	// return unit top-center (upper most pixel) offset from buffer origin
	return tuple(x_status_bar,y_status_bar);
}

// get art images count
vector<string> SpellUnitRec::GetArtList(FSarchive* info_fs)
{
	vector<string> list;

	// put first item	
	list.push_back(string(this->info));

	// load list file
	string list_name = string(this->info) + ".LST";
	uint8_t *data;
	int size;
	int fail = info_fs->GetFile(list_name.c_str(), &data, &size);
	if(!fail)
	{			
		// parse next items
		list.clear();
		list.push_back(string(this->info));
		string str;
		str.resize(size);
		memcpy(str.data(),data,size);
		auto ss = std::stringstream{str};
		for(std::string line; std::getline(ss,line,'\n');)
		{
			if(line.back() == '\r')
				line = line.substr(0,line.size()-1);
			list.push_back(line);
		}
	}
		
	return(list);
}

// get count of available arts for unit
int SpellUnitRec::GetArtCount(FSarchive* info_fs)
{
	auto list = GetArtList(info_fs);
	return(list.size());
}





//=============================================================================
// Unit bonuses stuff based on BONUSES.DEF file of COMMON.FS
//=============================================================================
UnitBonus::UnitBonus()
{
	level = 0;
	defence = 0;
	attack = 0;
	attack_count = 0;
	move = 0;
}


// load bonus data from BONUSES.DEF
UnitBonuses::UnitBonuses(string bonuses_def)
{
	// try load DEF data
	SpellDEF bonuses(bonuses_def);

	// for each bonus:
	list.resize(13);
	for(int k = 1; k <= 12; k++)
	{
		auto bonus = bonuses.GetSection(string_format("UnitLevel(%d)",k));
		if(!bonus)
			continue;

		for(auto& par : bonus->GetData())
		{
			if(par->parameters->empty())
				continue;
			if(!par->name.compare("Attack"))
				list[k].attack = std::stoi(par->parameters->at(0));
			else if(!par->name.compare("AttackPT"))
				list[k].attack_count = std::stoi(par->parameters->at(0));
			else if(!par->name.compare("Move"))
				list[k].move = std::stoi(par->parameters->at(0));
			else if(!par->name.compare("Defence"))
				list[k].defence = std::stoi(par->parameters->at(0));
		}

		delete bonus;
	}
}
// get bonus for given experience level
UnitBonus* UnitBonuses::GetBonus(int level)
{
	if(level > list.size())
		return(NULL);
	return(&list[level]);
}




//=============================================================================
// Map Unit object stuff
//=============================================================================
MapUnit::MapUnit(SpellMap *map)
{
	this->map = map;

	// unit idnetifier index within map
	id = 0;
	// unit type ID
	//type_id = 0;
	unit =  NULL;
	// position
	coor = MapXY();
	// experience
	experience = 0;
	experience_init = 0;
	experience_level = 1;
	// man count
	man = 1;
	// health (wounded men)
	wounded = 0;
	damage_remainder = 0;
	difficulty_adjusted = false;
	// morale (default full)
	morale = 100.0;
	// panic
	panic_turns = 0;
	// spec unit type
	spec_type = MapUnitType::Unknown;
	// unit behave
	behave = MapUnitType::NormalUnit;
	// custom name
	name.clear();
	// formation / commander metadata
	commander_id = 0;
	is_commander = 0;
	strategic_uid = 0;
	formation_id = 0;
	formation_commander_mask = 0;
	formation_level = 0;
	formation_attack_bonus = 0;
	formation_defence_bonus = 0;
	upgrade_move_bonus = 0;
	upgrade_defence_bonus = 0;
	upgrade_attack_bonus = 0;
	upgrade_attack_count_bonus = 0;
	upgrade_range_bonus = 0;
	// dig in
	dig_level = 0;
	dig_turns = 0;
	// turns since last activity
	idle_turns = 0;
	// action points
	action_points = 1;
	// unit active (set except insertion time)
	is_active = 0;
	// unit visible?
	hide = false;
	// enemy?
	is_enemy = 0;
	// unit being placed?
	in_placement = false;
	// moved
	was_moved = true;
	// created by event?
	is_event = false;
	// was unit already seen?
	was_seen = false;
	// is unit visible (0 - nope, 1 - yes (in last check), 2 - yes now)
	is_visible = 0;

	// next unit (for sorted rendering)
	next = NULL;
	// creator unit (if created ingame)
	parent = NULL;
	// child unit (if created ingame)
	child = NULL;

	// link to event that creates the unit (if exists)
	creator_event = NULL;
	// link to event to be triggered (SeeUnit)
	trig_events.clear();

	// sound refs
	sound_move = NULL;

	radar_up = false;

	altitude = 100;

	// FSU sprite index
	in_animation = NULL;
	azimuth = 0;
	azimuth_turret = 0;
	frame = 0;

	// view/attack maps
	view_map.assign(map->x_size*map->y_size, 0);
	attack_map.assign(map->x_size*map->y_size,0);

	move_state = MapUnit::MOVE_STATE::IDLE;
	attack_state = MapUnit::ATTACK_STATE::IDLE;
	action_state = MapUnit::ACTION_STATE::IDLE;
	action_step = 0;

	// Attack runtime state must never contain indeterminate pointers.  These
	// fields are transient (not part of a save) and are rebuilt when an attack
	// starts.  Leaving attack_target uninitialised makes any later cleanup/abort
	// path capable of treating random memory as a live MapUnit.
	attack_target = NULL;
	attack_target_obj.Clear();
	attack_hit_frame = 0;
	attack_hit_pnm = NULL;
	attack_fire_pnm = NULL;
	attack_fire_x_org = 0;
	attack_fire_y_org = 0;
	attack_fire_frame = 0;
	attack_proj_step = 0;
	attack_proj_delay = 0;
	is_target = false;

}

// copy without sound refs because delete would loose the sounds for source object!
MapUnit::MapUnit(MapUnit& obj,bool relink_event_trigger)
{
	*this = obj;
	sound_move = NULL;

	parent = NULL;
	child = NULL;
	creator_event = NULL;

	if(relink_event_trigger)
	{
		// disconnect event trigger from source unit, move it to new unit
		obj.trig_events.clear();
		for(auto &trig_event: trig_events)
			if(trig_event)
				trig_event->trig_unit = this;
	}
	else
		trig_events.clear();

	action_state = ACTION_STATE::IDLE;
	move_state = MOVE_STATE::IDLE;
	attack_state = ATTACK_STATE::IDLE;
	action_step = 0;
	attack_target = NULL;
	attack_target_obj.Clear();
	attack_hit_frame = 0;
	attack_hit_pnm = NULL;
	attack_fire_pnm = NULL;
	attack_fire_x_org = 0;
	attack_fire_y_org = 0;
	attack_fire_frame = 0;
	attack_proj_step = 0;
	attack_proj_delay = 0;
	is_target = false;
}

// clear sound refs
int MapUnit::ClearSounds()
{
	if(sound_move)
		delete sound_move;
	sound_move = NULL;
	return(0);
}
MapUnit::~MapUnit()
{
	ClearSounds();

	if(child)
	{
		// unlink from child unit
		child->parent = NULL;
		child = NULL;
	}
	if(parent)
	{
		// unlink from parent unit
		parent->child = NULL;
		parent = NULL;
	}
	for(auto &trig_event: trig_events)	
	{
		// unlink SeeUnit() event link
		if(trig_event)
			trig_event->trig_unit = NULL;
	}
}

// morph unit type to target (used e.g. for land/take off action)
int MapUnit::MorphUnit(SpellUnitRec* target, int health)
{
	if(!target || !unit)
		return 1;

	// Preserve health across transformations.  The DOS engine represents a
	// one-piece unit with a 0..999 fractional damage accumulator; infantry uses
	// active/wounded men.
	double active_frac = 1.0;
	double wounded_frac = 0.0;
	if(unit->isSingleMan())
	{
		active_frac = man > 0 ? (1000.0 - (std::clamp)(damage_remainder, 0, 999)) / 1000.0 : 0.0;
	}
	else if(unit->cnt > 0)
	{
		active_frac = (double)man / (double)unit->cnt;
		wounded_frac = (double)wounded / (double)unit->cnt;
	}
	if(health > 0)
	{
		active_frac = (std::clamp)(health / 100.0, 0.0, 1.0);
		wounded_frac = 0.0;
	}

	unit = target;
	if(unit->isSingleMan())
	{
		man = active_frac > 0.0 ? 1 : 0;
		wounded = 0;
		damage_remainder = man ? (std::clamp)((int)std::lround((1.0 - active_frac) * 1000.0), 0, 999) : 0;
	}
	else
	{
		man = (std::clamp)((int)std::lround(active_frac * unit->cnt), 0, unit->cnt);
		wounded = (std::clamp)((int)std::lround(wounded_frac * unit->cnt), 0, unit->cnt - man);
		damage_remainder = 0;
	}

	radar_up = false;
	if(unit->isAir())
		dig_level = 0;

	if(AreSoundsDone())
		ClearSounds();
	return 0;
}

// try update dig level if possible (call before end of turn)
int MapUnit::UpdateDigLevel()
{
	int maxdig = unit->GetMaxDig();

	if (dig_level > maxdig)
	{
		dig_level = maxdig;
		dig_turns = 0;
		return 0;
	}

	if (dig_level >= maxdig)
		return 0;
	if (dig_turns < unit->dig_turns)
		return 0;

	dig_level++;
	if (dig_level > maxdig) dig_level = maxdig;

	dig_turns = 0;
	return 1;
}


// clear dig level
void MapUnit::ClearDigLevel()
{
	dig_level = 0;
	dig_turns = 0;
}
// clears unit idle and dig counters
//void MapUnit::ResetTurnsCounter()
//{
//	dig_turns = 0;
//	idle_turns = 0;
//}

void MapUnit::ResetTurnsCounter()
{
	// -1 je �mysln�: v End-of-turn se v�dy inkrementuje,
	// tak�e jednotka co n�co d�lala skon�� po inkrementu na 0 (ne na 1).
	dig_turns = -1;
	idle_turns = -1;
}


// increment turns counetrs
void MapUnit::IncrementTurnsCounter()
{
	dig_turns++;
	idle_turns++;
}
// activate unit (after runtime creation)
void MapUnit::ActivateUnit()
{
	is_active = true;
}
// is unit active (after runtime creation)
int MapUnit::isActive()
{
	return(!!is_active);
}


// set full health
int MapUnit::ResetHealth()
{
	wounded = 0;
	damage_remainder = 0;
	man = unit->cnt;
	return(0);
}

// action points for this level of experience
int MapUnit::GetMaxAP()
{
	// basic AP
	int ap = unit->ap;

	// experience bonuses
	auto bonus = unit->bonuses->GetBonus(experience_level);
	ap += (bonus->move + upgrade_move_bonus) * unit->apw;

	return(ap);
}

// has unit full AP?
int MapUnit::HasMaxAP()
{
	return(action_points == GetMaxAP());
}

// reset unit action points
int MapUnit::ResetAP()
{
	action_points = GetMaxAP();
	return(action_points);
}

// get AP per single shot
int MapUnit::GetAPperFire()
{
	// get max fires count
	int ap = GetMaxAP();
	int basic_fires = GetMaxFireCount();
	if(!basic_fires)
		return(0);

	// get ap per fire for this level of experience
	return(floor(ap/basic_fires));
}

// get fires count
int MapUnit::GetFireCount(int ext_ap)
{
	int ap_per_fire = GetAPperFire();
	if(!ap_per_fire)
		return(0);
	if(unit->isActionKamikaze())
		return(action_points != 0);
	
	// actual fires count
	if(ext_ap >= 0)		
		return(floor(ext_ap/ap_per_fire));
	return(floor(action_points/ap_per_fire));
}

// get max fires count
int MapUnit::GetMaxFireCount()
{
	// get max fires count
	if(unit->isActionKamikaze())
		return(1);
	int ap = GetMaxAP();
	if(!unit->aps)
		return(0);
	int basic_fires = floor(unit->ap/unit->aps);
	
	// add bonus
	auto bonus = unit->bonuses->GetBonus(experience_level);
	basic_fires += bonus->attack_count + upgrade_attack_count_bonus;
	
	return(basic_fires);
}

// reduce AP by single fire
int MapUnit::UpdateFireAP()
{
	int ap_per_fire = GetAPperFire();
	if(!ap_per_fire)
		return(1);
	if(action_points >= ap_per_fire)
		action_points -= ap_per_fire;	
	return(0);
}

// get AP per walk
int MapUnit::GetWalkAP()
{
	int move_range = unit->apw;
	auto bonus = unit->bonuses->GetBonus(experience_level);
	move_range += bonus->move + upgrade_move_bonus;
	return(GetMaxAP()/(move_range-1));
}

// has unit wounded men?
int MapUnit::HasWounded()
{
	if(unit && unit->isSingleMan())
		return(damage_remainder > 0);
	return(wounded > 0);
}

// heal/repair unit.  Infantry converts wounded men back to active men; the
// original single-man damage accumulator is restored to zero for vehicles and
// other one-piece units.  The action consumes the whole turn as before.
int MapUnit::Heal()
{
	if(!unit)
		return 0;

	int healed = 0;
	if(unit->isSingleMan())
	{
		if(damage_remainder <= 0)
			return 0;
		healed = damage_remainder;
		damage_remainder = 0;
	}
	else
	{
		if(wounded <= 0)
			return 0;
		const int max_men = unit->cnt;
		healed = wounded;
		man = (std::min)(man + healed, max_men);
		wounded = (std::max)(wounded - healed, 0);
	}

	action_points = 0;
	ResetTurnsCounter();
	return healed;
}

// can do special action (if enough AP)
int MapUnit::CanSpecAction()
{
	if(!unit->hasSpecAction())
		return(false);
	return(action_points >= unit->action_ap);
}

// initialize experience when unit is created
int MapUnit::InitExperience(int level)
{
	// set experience level
	experience_init = (std::min)((std::max)(level,0),12);
	experience_level = (std::min)((std::max)(level,1),12);
	
	// generate random experience points based on level
	// note: crude approximation of Spellcross, it seems actual experience is always somewhere around 20% of current level
	int lev_a = unit->GetExperiencePts(level);
	int lev_b = unit->GetNextExperiencePts(level);
	experience = lev_a + (10 + rand()%11)*(lev_b - lev_a)/100;

	return(experience);
}
// add points of experience (updates experience level)
int MapUnit::AddExperience(int points)
{
	int old_level = experience_level;
	experience = (std::min)(experience + points, unit->GetExperiencePts(12));
	experience_level = 1;
	while(experience >= unit->GetNextExperiencePts(experience_level))
		experience_level++;
	return(old_level != experience_level);
}
// add points of experience based on target unit killed men
int MapUnit::AddExperience(MapUnit* target,int killed)
{	
	return(AddExperience((killed*(std::max)(target->experience,target->unit->GetNextExperiencePts(1)/2))/target->unit->cnt));
}
// update morale level with limits protection
int MapUnit::UpdateModale(double points)
{
	double old = morale;
	morale = (std::max)((std::min)(morale + points,100.0),0.0);

	// Arm panic only on transition from >0 to 0 (so it happens once)
	if (old > 0.0 && morale <= 0.0)
		panic_turns = 2;

	// flee level?
	return(morale < 25.0);
}


int MapUnit::Render(Terrain* data,uint8_t* buffer,uint8_t* buf_end,int buf_x_pos,int buf_y_pos,int buf_x_size,uint8_t *filter,uint8_t* hud_filter,Sprite* sprt,int show_hud)
{
	if(hide)
		return(0);	
	
	// filter for shadow rendering
	auto shadow_filter = data->filter.darker;
	
	int loc_frame = frame;
	if(!in_animation)
		loc_frame = -1; // static unit resource
	
	// visible man based on health
	int visible_man = (unit->vis - 1)*(wounded + man)/unit->GetHP() + 1;

	// render unit
	auto [x_status_bar,y_status_bar] = unit->Render(buffer, buf_end, buf_x_pos, buf_y_pos, buf_x_size,filter,shadow_filter, sprt, visible_man,azimuth, azimuth_turret, loc_frame, in_animation, altitude);
	
	if(!hud_filter)
		hud_filter = data->filter.nullpal;

	// -- make status bar
	if(!show_hud)
		return(y_status_bar);
	const int bar_w = 28;
	const int bar_h = 15;
	SpellMap* map_ptr = this->map;
	// check valid rendering range
	auto *psb     = &buffer[(buf_x_pos + x_status_bar - bar_w/2) + (buf_y_pos + y_status_bar - bar_h/2)*buf_x_size];
	auto *psb_end = &buffer[(buf_x_pos + x_status_bar - bar_w/2 + bar_w-1) + (buf_y_pos + y_status_bar - bar_h/2 + bar_h-1)*buf_x_size];
	if(psb < buffer || psb_end >= buf_end)
		return(0);

	// render shadow background
	for(int y = 0; y < bar_h; y++)
	{
		uint8_t* buf = &psb[y*buf_x_size];
		for(int x = 0; x < bar_w; x++)
			buf[x] = hud_filter[shadow_filter[buf[x]]];
	}

	// hitpoints bar size
	int hp_w;
	if(is_enemy)
		hp_w = 26;
	else
		hp_w = (commander_id || is_commander)?17:26;
	int hp_h = 3;
	
	// hit points (in pixels)
	int hp = 0;

	// Original single-man units keep damage in thousandths rather than in the
	// infantry wounded counter.
	if (unit->isSingleMan())
	{
		const int hp_cur = (std::max)(0, 1000 - damage_remainder);
		hp = (hp_w * hp_cur) / 1000;
	}
	else
	{
		hp = (hp_w * man) / unit->cnt;
	}

	// clamp (ochrana)
	hp = (std::max)(0, (std::min)(hp_w, hp));

	// action points (in pixels)
	int ap = (std::min)((hp_w*action_points)/GetMaxAP(),hp_w);
	
	// render action point (aliance only)
	if(!is_enemy)
	{
		int ap_color = (is_active)?230:228;
		for(int y = 1; y < 4; y++)
		{
			uint8_t* buf = &psb[y*buf_x_size];
			for(int x = 1; x <= ap; x++)
				buf[x] = hud_filter[ap_color];
		}
	}
	
	// render hitpoints
	int hp_color = (is_active)?234:233;
	if(is_enemy)
		hp_color = 253;
	for(int y = 5; y < 8; y++)
	{
		uint8_t* buf = &psb[y*buf_x_size];
		for(int x = 1; x <= hp; x++)
			buf[x] = hud_filter[hp_color];
	}

	// render special unit type mark
	int type_color = 0;
	if(spec_type == MapUnitType::MissionUnit)
		type_color = 230;
	else if(spec_type == MapUnitType::SpecUnit)
		type_color = 253;
	for(int y = 9; y < bar_h; y+=2)
	{
		uint8_t* buf = &psb[y*buf_x_size];
		for(int x = bar_w-3; x < bar_w-1; x++)
			buf[x] = hud_filter[type_color];
	}

	// render fire count
	if(!is_enemy)
	{
		int fires = GetFireCount();
		for(int k = 0; k < fires; k++)
		{		
			uint8_t* buf = &psb[9*buf_x_size + 1 + k*4];
			buf[0] = hud_filter[253]; buf[1] = hud_filter[253]; buf[2] = hud_filter[202];
			buf += buf_x_size;
			buf[0] = hud_filter[253]; buf[1] = hud_filter[202]; buf[2] = hud_filter[202];
		}
	}

	// render dig level
	for(int k = 0; k < dig_level; k++)
	{
		uint8_t* buf = &psb[12*buf_x_size + 1 + k*4];
		buf[0] = hud_filter[252]; buf[1] = hud_filter[252]; buf[2] = hud_filter[214];
		buf += buf_x_size;
		buf[0] = hud_filter[252]; buf[1] = hud_filter[214]; buf[2] = hud_filter[214];
	}

	if((commander_id || is_commander) && !is_enemy)
	{
		// render active formation id (disappears immediately when formation breaks)
		if(commander_id > 0)
			data->font7->RenderSymbol(psb, psb_end, buf_x_size, 19, 1, '0'+commander_id,hud_filter[232]);

		// the host mark survives even when losses temporarily break the formation
		if(is_commander)
			data->font7->RenderSymbol(psb,psb_end,buf_x_size,24,1,31,hud_filter[232]);

	}
	

	// GROUP MODE outline: red rectangle around whole status bar (like original)
	if(map && map->IsGroupMode() && map->IsUnitInActiveGroup(this))
	{
		uint8_t col = hud_filter[253];
		// psb points to top-left of bar area
		for(int x = 0; x < bar_w; x++)
		{
			psb[x] = col;
			psb[(bar_h-1)*buf_x_size + x] = col;
		}
		for(int y = 0; y < bar_h; y++)
		{
			psb[y*buf_x_size] = col;
			psb[y*buf_x_size + (bar_w-1)] = col;
		}
	}


	// group mode highlight: red outline around the whole status bar (as in original)
	if (map_ptr && map_ptr->IsGroupMode() && map_ptr->IsUnitInActiveGroup(this) && !is_enemy)
	{
		const uint8_t col = hud_filter[253];
		// top/bottom
		for (int x = 0; x < bar_w; x++)
		{
			psb[0 * buf_x_size + x] = col;
			psb[(bar_h - 1) * buf_x_size + x] = col;
		}
		// left/right
		for (int y = 0; y < bar_h; y++)
		{
			psb[y * buf_x_size + 0] = col;
			psb[y * buf_x_size + (bar_w - 1)] = col;
		}
	}

	// return top pixel of unit
	return(y_status_bar);
}



// render vertical bar indictator (for GUIs)
void MapUnit::RenderVertBar(uint8_t* buffer,uint8_t* buf_end,int buf_x_size,int pos_x,int pos_y,int size_x,int size_y,double level,uint8_t color)
{
	if (level <= 0.0)
		return;
	int pix = (std::min)((std::max)((int)(level*size_y), 1),size_y);
	for(int y = pos_y + size_y - 1; y > pos_y + size_y - pix; y--)
		for(int x = pos_x; x < pos_x+size_x; x++)
		{
			auto *pix = &buffer[x + y*buf_x_size];
			if(pix < buffer || pix >= buf_end)
				continue;
			*pix = color;
		}
}

// render unit preview for map units list
int MapUnit::RenderPreview(uint8_t* buffer,uint8_t* buf_end,int buf_x_size)
{
	auto *spell_data = map->spelldata;

	// render background
	auto *back = spell_data->gres.wm_map_units_list;
	back->Render(buffer, buf_end,buf_x_size,0,0);

	// title (pos = 4,3, size = 137,16)
	int title_color = (is_enemy)?212:232;
	std::string unit_name = name;
	if(unit_name.empty())
		unit_name = unit->name;
	spell_data->font->Render(buffer, buf_end, buf_x_size, 4,3, 137,16, unit_name,title_color, 254, SpellFont::FontShadow::DIAG3);

	// icon (pos = 82,23)
	unit->icon_glyph->Render(buffer, buf_end, buf_x_size, 82,23);

	// morale (pos = 4,23, size = 6,41)
	RenderVertBar(buffer, buf_end, buf_x_size, 4,23, 6,41, 0.01*morale, 196);
	// label (pox = 6,50)
	spell_data->font->Render(buffer,buf_end,buf_x_size,6,50,string_format("%02.0f",morale),252,254,SpellFont::FontShadow::RIGHT_DOWN);

	// ap (pos = 28,23, size = 6,41)
	RenderVertBar(buffer, buf_end, buf_x_size, 28,23, 6,41, (double)action_points/GetMaxAP(), 228);
	// label (pox = 30,50)
	spell_data->font->Render(buffer,buf_end,buf_x_size,30,50,string_format("%02d",action_points),230,254,SpellFont::FontShadow::RIGHT_DOWN);

	// fire count (pos1 = 36,48, y_step = 5)
	for(int k = 0; k < GetFireCount(); k++)
		spell_data->gres.red_led_on->Render(buffer,buf_end,buf_x_size,36,48-k*5);
	for(int k = GetFireCount(); k < GetMaxFireCount(); k++)
		spell_data->gres.red_led_off->Render(buffer,buf_end,buf_x_size,36,48-k*5);
	 
	// hp (pos = 52,23, size = 6,41)
	int hp_color = (is_enemy)?253:235;
	//int hp_txt_color = (is_enemy)?211:235;
	RenderVertBar(buffer,buf_end,buf_x_size,52,23,6,41,(double)(man+wounded)/unit->cnt,216);
	RenderVertBar(buffer,buf_end,buf_x_size,52,23, 6,41,(double)man/unit->cnt,hp_color);
	// max man (pox = 60,50)
	spell_data->font->Render(buffer,buf_end,buf_x_size,60,50,string_format("%02d",unit->cnt),216,254,SpellFont::FontShadow::RIGHT_DOWN);
	// hp (pox = 60,36)
	spell_data->font->Render(buffer,buf_end,buf_x_size,60,36,string_format("%02d",man),hp_color,254,SpellFont::FontShadow::RIGHT_DOWN);
	// wounded (pox = 60,22)
	spell_data->font->Render(buffer,buf_end,buf_x_size,60,22,string_format("%02d",wounded),215,254,SpellFont::FontShadow::RIGHT_DOWN);

	// experience (pox = 5,66, y_step = 9)
	for(int k = 0; k < experience_level; k++)
		spell_data->gres.wm_exp_mark->Render(buffer,buf_end,buf_x_size,5 + k*9,66);
	
	if(!map->isGameMode())
	{
		// unit ID (pos = 141,25)
		spell_data->font7->SetFilter(map->terrain->filter.darkpal);
		spell_data->font7->Render(buffer,buf_end,buf_x_size,141,25,string_format("#%d",id),252,254,SpellFont::FontShadow::SOLID,SpellFont::FontAlign::RIGHT);
		spell_data->font7->SetFilter(NULL);
	}
		
	return(0);
}



// return unit animation associated with attack to particular unit (or object)
FSU_resource *MapUnit::GetShotAnim(MapUnit *target, int *frame_stop)
{	
	FSU_resource *fsu_anim = NULL;
	int frames = 0;
	if(!target && unit->gr_attack_armor || target && target->unit->isArmored())
	{
		fsu_anim = unit->gr_attack_armor;
		frames = unit->anim_atack_armor_frames;
	}
	else if(!target && unit->gr_attack_light || target && target->unit->isLight())
	{
		fsu_anim = unit->gr_attack_light;
		frames = unit->anim_atack_light_frames;
	}
	else if(!target && unit->gr_attack_air || target && target->unit->isAir())
	{
		fsu_anim = unit->gr_attack_air;
		frames = unit->anim_atack_air_frames;
	}
	// if unit-def frame count is missing/zero, fall back to actual resource frames
	if (fsu_anim && frames <= 0)
		frames = fsu_anim->anim.frames;

	if(frame_stop)
		*frame_stop = frames;
	return(fsu_anim);
}

// return target hit animation if exist
AnimPNM *MapUnit::GetTargetHitPNM(MapUnit *target)
{
	if(!target && unit->pnm_armored_shot || target && target->unit->isArmored())
		return(unit->pnm_armored_shot);
	if(!target && unit->pnm_light_shot || target && target->unit->isLight())
		return(unit->pnm_light_shot);	
	if(!target && unit->pnm_air_shot || target && target->unit->isAir())
		return(unit->pnm_air_shot);
	return(NULL);
}

// return shot animation if exist (this is used to animate e.g. cannon fireball)
AnimPNM* MapUnit::GetFirePNM(MapUnit* target)
{
	if(!target && unit->pnm_armored_hit || target && target->unit->isArmored())
		return(unit->pnm_armored_hit);
	if(!target && unit->pnm_light_hit || target && target->unit->isLight())
		return(unit->pnm_light_hit);
	if(!target && unit->pnm_air_hit || target && target->unit->isAir())
		return(unit->pnm_air_hit);
	return(NULL);
}


// return shot animation if exist (this is used to animate e.g. cannon fireball)
tuple<int,int> MapUnit::GetFirePNMorigin(MapUnit* target, double azimuth)
{
	AnimPNM* pnm = GetFirePNM(target);
	/*if(!pnm)
		return tuple(0,0);*/
	if(pnm && unit->gr_aux)
		azimuth = round(azimuth/(double)unit->gr_aux->stat.azimuths)*(double)unit->gr_aux->stat.azimuths;

	int x = (int)(+cos(azimuth/180.0*M_PI)*SpellUnitRec::FIRE_RING_DIAMETER);
	int y = (int)(-sin(azimuth/180.0*M_PI)*SpellUnitRec::FIRE_RING_DIAMETER*cos(Sprite::PROJECTION_ANGLE/180.0*M_PI));
	return tuple(x,y);
}


// check if all unit sounds are stoped (e.g. test before sounds clear when morphing unit)
int MapUnit::AreSoundsDone()
{	
	if(sound_move)
		if(!sound_move->isDone())
			return(false);
	return(true);
}

// play report sound
int MapUnit::PlayReport()
{
	auto sound_report = new SpellSound(*unit->sound_report);
	sound_report->Play(true);
	return(0);
}

// play contact sound
int MapUnit::PlayContact()
{
	auto sound_contact = new SpellSound(*unit->sound_contact);
	sound_contact->Play(true);
	return(0);
}

// play being hit sound
int MapUnit::PlayBeingHit()
{
	auto sound_hit = new SpellSound(*unit->sound_hit);
	sound_hit->Play(true);
	return(0);
}

// play die sound
int MapUnit::PlayDie()
{
	auto sound_die = new SpellSound(*unit->sound_die);
	sound_die->Play(true);
	return(0);
}

// play move sound
int MapUnit::PlayMove()
{
	if(!sound_move)
		sound_move = new SpellSound(*unit->sound_move);
	sound_move->Play();
	return(0);
}
// stop move sound
int MapUnit::PlayStop()
{
	sound_move->StopMove();
	return(0);
}

// play fire sound to given unit type
int MapUnit::PlayFire(MapUnit* target)
{
	SpellSound *sound = NULL;
	if(!target && unit->sound_attack_armor || target && target->unit->isArmored())
		sound = new SpellSound(*unit->sound_attack_armor->shot);
	else if(!target && unit->sound_attack_light->shot || target && target->unit->isLight())
		sound = new SpellSound(*unit->sound_attack_light->shot);
	else if(!target && unit->sound_attack_air->shot || target && target->unit->isAir())
		sound = new SpellSound(*unit->sound_attack_air->shot);
	if(sound)
		sound->Play(true);
	return(0);
}


// play target hit sound to given unit type (or object if target=NULL)
int MapUnit::PlayHit(bool missed)
{
	return(PlayHit(NULL,missed));
}
int MapUnit::PlayHit(MapUnit* target, bool missed)
{	
	SpellAttackSound *attack = NULL;
	if(!target && unit->sound_attack_armor)
		attack = unit->sound_attack_armor;
	else if(!target && unit->sound_attack_light)
		attack = unit->sound_attack_light;
	else if(!target && unit->sound_attack_air)
		attack = unit->sound_attack_air;
	else if(target->unit->isLight())
		attack = unit->sound_attack_light;
	else if(target->unit->isArmored())
		attack = unit->sound_attack_armor;
	else if(target->unit->isAir())
		attack = unit->sound_attack_air;
	if(!attack)
		return(1);

	SpellSound* sound = NULL;
	if(missed)
		sound = attack->hit_miss;
	else if(!target)
		sound = attack->hit_armor;
	else if(target->unit->isFlashAndBones())
		sound = attack->hit_flash;
	else if(target->unit->isMetal())
		sound = attack->hit_armor;	
	if(!sound)
		return(1);

	sound = new SpellSound(*sound);
	sound->Play(true);
	return(0);
}

// play report sound
int MapUnit::PlayAction()
{
	if(!unit || !unit->sound_action)
		return(1);
	auto sound_action = new SpellSound(*unit->sound_action);
	sound_action->Play(true);
	return(0);
}

// play level up sound
int MapUnit::PlayLevelUp()
{
	if(!unit || !unit->sound_level_up)
		return(1);
	auto sound = new SpellSound(*unit->sound_level_up);
	sound->Play(true);
	return(0);
}



// --- attack damage model stuff

// get attack strength to target unit (or NULL to object)
int MapUnit::GetAttack(MapUnit *target)
{
	TARGET_TYPE type = TARGET_TYPE::NONE;
	if(!target)
		type = TARGET_TYPE::OBJECT;
	else if(target->unit->isLight())
		type = TARGET_TYPE::LIGHT;
	else if(target->unit->isArmored())
		type = TARGET_TYPE::ARMOR;
	else if(target->unit->isAir())
		type = TARGET_TYPE::AIR;
	return(GetAttack(type));
}
int MapUnit::GetAttack(MapUnit::TARGET_TYPE target)
{
	// get basic attack
	int attack = 0;
	if(target == TARGET_TYPE::OBJECT)
		attack = unit->attack_objects;
	else if(target == TARGET_TYPE::LIGHT)
		attack = unit->attack_light;
	else if(target == TARGET_TYPE::ARMOR)
		attack = unit->attack_armored;
	else if(target == TARGET_TYPE::AIR)
		attack = unit->attack_air;
	if(!attack)
		return(attack);
	
	// add experience + active strategic-formation bonuses.
	// FORMACIE.DEF defines the direct bonus for the highest active formation:
	// battalion +1 attack, regiment +2, brigade +4.
	auto bonus = unit->bonuses->GetBonus(experience_level);
	attack += bonus->attack;
	attack += formation_attack_bonus;
	attack += upgrade_attack_bonus;
		
	return(attack);
}

// get unit defence
int MapUnit::GetDefence()
{
	// basic defence
	int defence = unit->defence;
	if(!defence)
		return(defence);
	
	// add experience + active strategic-formation bonuses.
	defence += unit->bonuses->GetBonus(experience_level)->defence;
	defence += formation_defence_bonus;
	defence += upgrade_defence_bonus;
	return(defence);
}


// apply damage model for attack to target tile/object
MapUnit::AttackResult MapUnit::DamageTarget(MapSprite *target)
{
	if(!target)
		return(AttackResult::Missed);

	ResetTurnsCounter();

	// being attacked should interrupt digging/idle progress (even if the shot misses)
	if (auto* tu = dynamic_cast<MapUnit*>(target))
	{
		tu->ResetTurnsCounter();
	}

	// attack strength
	double attack = GetAttack();
	
	// object's defence
	auto tile = target->GetDestructible();
	double defence = tile->destructible->defence;

	// damage model
	int hit = (int)(randgman(3.0, 2.0, 5.0)*attack - defence);
	if(hit < 0)
		return(AttackResult::Missed);
	
	// reduce HP
	target->hp = (std::max)(target->hp - hit, 0);

	if(!target->hp)
		return(AttackResult::Kill);
	else
		return(AttackResult::Hit);
}

// apply damage model for attack to target unit
MapUnit::AttackResult MapUnit::DamageTarget(MapUnit* target)
{
	if(!target || !unit || !target->unit)
		return AttackResult::Missed;

	ResetTurnsCounter();
	target->ResetTurnsCounter();

	const int base_attack = GetAttack(target);
	if(base_attack <= 0)
	{
		// In the original game even an incoming shot interrupts digging.
		target->dig_level = (std::max)(target->dig_level - 1, 0);
		return AttackResult::Missed;
	}

	auto rnd = [](int exclusive) -> int
	{
		return exclusive > 0 ? (std::rand() % exclusive) : 0;
	};
	auto rank_of = [](const MapUnit* u) -> int
	{
		return u ? (std::clamp)(u->experience_level - 1, 0, 11) : 0;
	};

	// ---------------------------------------------------------------------
	// 1) ORIGINAL HIT CHANCE (SPELCROS.EXE 0x7C690)
	// ---------------------------------------------------------------------
	const int base_defence = target->GetDefence();
	const int effective_defence = base_defence + 5 * (std::max)(0, target->dig_level);
	int hit_chance = 100;

	if(!unit->isActionKamikaze() && !(unit->hasFireAttack() && target->unit->isFireSensitive()))
	{
		const int diff = base_attack - effective_defence;
		if(diff >= 0)
		{
			if(diff > 8)
				hit_chance = 100;
			else
				hit_chance = 80 + (diff * diff * 80) / 64;
		}
		else
		{
			const int d = -diff;
			if(d > 12)
				hit_chance = 20;
			else
				hit_chance = 80 - (d * d * 60) / 144;
		}

		// Morale below 90 scales accuracy from 60% at zero morale to 100% at 90.
		if(morale < 90.0)
		{
			const int m = (std::clamp)((int)std::lround(morale), 0, 90);
			const int factor = 60 + (m * 40) / 90;
			hit_chance = (hit_chance * factor) / 100;
		}
	}

	if(map && is_enemy != target->is_enemy)
	{
		const auto difficulty = map->GetGameDifficulty();
		if(difficulty == SpellMap::GameDifficulty::EASY && !is_enemy && target->is_enemy)
			hit_chance += rnd(16);
		else if(difficulty == SpellMap::GameDifficulty::HARD && is_enemy && !target->is_enemy)
			hit_chance += rnd(16);
	}
	hit_chance = (std::clamp)(hit_chance, 20, 100);

	// The original loses one entrenchment level after every incoming shot,
	// including a miss.
	target->dig_level = (std::max)(target->dig_level - 1, 0);

	if(rnd(100) > hit_chance)
		return AttackResult::Missed;

	// ---------------------------------------------------------------------
	// 2) ORIGINAL EFFECT / LETHALITY (SPELCROS.EXE 0x7C9B8)
	// Normal attacks randomize attack and defence independently by 0..5 here.
	// ---------------------------------------------------------------------
	int effect_attack = base_attack + rnd(6);
	int effect_defence = effective_defence + rnd(6);
	int lethality = 0;
	const int effect_diff = effect_attack - effect_defence;
	if(effect_diff >= 0)
	{
		if(effect_diff > 14)
			lethality = 100;
		else
			lethality = 16 + (effect_diff * effect_diff * 84) / 196;
	}
	else
	{
		const int d = -effect_diff;
		if(d > 10)
			lethality = 1;
		else
			lethality = 16 - (d * d * 16) / 100;
	}

	if(unit->hasFireAttack() && target->unit->isFireSensitive())
		lethality += 10;

	if(map && is_enemy != target->is_enemy)
	{
		const auto difficulty = map->GetGameDifficulty();
		if(difficulty == SpellMap::GameDifficulty::HARD)
			lethality += is_enemy ? rnd(6) : -rnd(6);
		else if(difficulty == SpellMap::GameDifficulty::EASY)
			lethality += is_enemy ? -rnd(6) : rnd(6);
	}
	lethality = (std::clamp)(lethality, 4, 100);

	// AP-draining attacks (fire flag 0x04) use the same hit/effect rolls but
	// branch out before the casualty model in SPELCROS.EXE 0x785B6.
	if(unit->stealsActionPoints())
	{
		const int ap_loss = (std::max)(1, (target->GetMaxAP() * lethality) / 100);
		target->action_points = (std::max)(0, target->action_points - ap_loss);
		return AttackResult::Hit;
	}

	// ---------------------------------------------------------------------
	// 3) ORIGINAL PARTIAL-DAMAGE / CASUALTY ACCUMULATOR
	// SPELCROS.EXE 0x786E2..0x789B7 starts from
	//     target_active * lethality
	// and multiplies it by a current-strength ratio.  Single-piece units use
	// a 0..999 sub-unit damage accumulator instead of a fractional man count.
	// ---------------------------------------------------------------------
	const int attacker_max = (std::max)(1, unit->cnt);
	const int target_max = (std::max)(1, target->unit->cnt);
	const int attacker_active = (std::max)(0, man);
	const int target_active = (std::max)(0, target->man);
	if(attacker_active <= 0 || target_active <= 0)
		return AttackResult::Hit;

	double numerator = 0.0;
	double denominator = 1.0;
	if(attacker_max != 1 && target_max != 1)
	{
		numerator = (double)attacker_active * (double)target_max;
		denominator = (double)attacker_max * (double)target_active;
	}
	else if(attacker_max != 1 && target_max == 1)
	{
		numerator = (double)attacker_active * 1000.0;
		denominator = (double)(1000 - (std::clamp)(target->damage_remainder, 0, 999)) * (double)attacker_max;
	}
	else if(attacker_max == 1 && target_max != 1)
	{
		numerator = (double)(1000 - (std::clamp)(damage_remainder, 0, 999)) * (double)target_max;
		denominator = (double)target_active * 1000.0;
	}
	else
	{
		numerator = (double)(1000 - (std::clamp)(damage_remainder, 0, 999)) * 1000.0;
		denominator = (double)(1000 - (std::clamp)(target->damage_remainder, 0, 999)) * 1000.0;
	}
	if(denominator <= 0.0)
		return AttackResult::Hit;

	double raw_effect = ((double)target_active * (double)lethality) * (numerator / denominator) / 100.0;
	raw_effect += (double)(std::clamp)(target->damage_remainder, 0, 999) / 1000.0;

	int affected = (int)raw_effect; // DOS helper truncates positive values toward zero
	const int raw_affected = affected;
	int new_remainder = (int)((raw_effect - (double)affected) * 1000.0);
	target->damage_remainder = (std::clamp)(new_remainder, 0, 999);

	// A hit may only add fractional damage this time.  It still counts as a hit.
	if(affected <= 0)
	{
		// Original morale damage is based on the hit's lethality, not only kills.
		int morale_loss = ((19 - rank_of(target)) * lethality) / 50;
		if(target->is_enemy)
			morale_loss /= 4;
		if(morale_loss <= 0) morale_loss = 1;
		target->UpdateModale(-(double)morale_loss);
		return AttackResult::Hit;
	}

	const int before_active = target->man;
	affected = (std::clamp)(affected, 0, before_active);

	// Original killed-vs-wounded split.  Hard adds a second 0..39% kill roll.
	double kill_fraction = (double)(lethality + rnd(40)) / 100.0;
	if(map && map->GetGameDifficulty() == SpellMap::GameDifficulty::HARD)
		kill_fraction += (double)rnd(40) / 100.0;
	if(unit->type_id == 0x3c)
		kill_fraction = 1.0;
	kill_fraction = (std::clamp)(kill_fraction, 0.0, 1.0);
	int killed = (int)(kill_fraction * affected);
	killed = (std::clamp)(killed, 0, affected);

	if(target->unit->isSingleMan())
	{
		// One accumulated whole unit of effect destroys the vehicle/monster.
		if(affected >= 1)
		{
			target->man = 0;
			target->wounded = 0;
			target->damage_remainder = 0;
		}
	}
	else
	{
		target->man = (std::max)(0, target->man - affected);
		target->wounded += (affected - killed);
		if(target->man <= 0)
			target->wounded = 0;
	}

	int level_up = false;
	if(killed > 0)
		level_up = AddExperience(target, killed);
	if(level_up)
		PlayLevelUp();

	// Original target morale loss (0x7CDAF).
	int morale_loss = ((19 - rank_of(target)) * lethality) / 50;
	if(target->is_enemy)
		morale_loss /= 4;
	if(morale_loss <= 0) morale_loss = 1;
	target->UpdateModale(-(double)morale_loss);

	// Original attacker morale gain (0x7CE51), tied to relative experience and
	// the severity of the hit.  Keep it integer-like just as the DOS routine did.
	if(affected > 0)
	{
		const int ar = rank_of(this);
		const int tr = rank_of(target);
		int gain_base = 0;
		if(tr > ar)
			gain_base = lethality * (tr - ar);
		else
			gain_base = lethality / (ar - tr + 1);
		int morale_gain = gain_base / (ar + 1);
		morale_gain = (std::clamp)(morale_gain, 1, 30);
		// DOS grants the destruction bonus only when the un-clamped calculated
		// effect exceeds the target's remaining active count.
		if(before_active < raw_affected)
			morale_gain += 10 + 10 / (ar + 1);
		if(is_enemy)
			morale_gain *= 2;
		UpdateModale((double)morale_gain);
	}

	// Existing special morale/fear/paralyze actions remain wired to the remaster
	// action system.  Their dedicated DOS formulas are a separate subsystem.
	const int act = unit->action_id;
	const bool has_morale_spec =
		(act == SpellUnitRec::SPEC_ACT_LOWER_MORALE ||
		 act == SpellUnitRec::SPEC_ACT_DRAGON_FEAR ||
		 act == SpellUnitRec::SPEC_ACT_PARALYZE);
	if(has_morale_spec)
	{
		int radius = unit->action_params[0];
		int level = unit->action_params[1];
		if(radius <= 0) radius = 1;
		const double drop = (level > 0) ? (level * 5.0) : 15.0;
		if(!map || radius <= 1)
			target->UpdateModale(-drop);
		else
		{
			const MapXY center = (act == SpellUnitRec::SPEC_ACT_PARALYZE) ? coor : target->coor;
			for(auto* u : map->units)
			{
				if(!u || u->is_enemy == is_enemy || u->coor.Distance(center) > radius) continue;
				u->UpdateModale(-drop);
			}
		}
	}

	return target->man <= 0 ? AttackResult::Kill : AttackResult::Hit;
}

// check if unit is dead
int MapUnit::isDead() const
{
	return(!man);
}

// kill unit (exec linked events, then delete the object, so no touchy afterwards and call Extract() from units list before calling this!)
SpellMapEventRec *MapUnit::Kill()
{
	SpellMapEventRec *evt = NULL;
	for(auto &trig_event: trig_events)
	{		
		// unlink from event
		evt = trig_event;
		evt->trig_unit = NULL;
		trig_event = NULL;

		// For objective events (TransportUnit, SaveUnit, etc.) killing the trigger unit
		// means the objective FAILED, not succeeded — do NOT mark as done.
		if (!evt->is_objective)
			evt->is_done = true;
	}
	// clear so destructor won't re-process
	trig_events.clear();

	// destroy this unit
	delete this;

	// eventually return triggered event
	return(evt);
}

// try remove trigger event from list
int MapUnit::RemoveTrigEvent(SpellMapEventRec* event)
{
	auto evt_id = std::find(trig_events.begin(),trig_events.end(),event);
	if(evt_id == trig_events.end())
		return(1);
	trig_events.erase(evt_id);
	return(0);
}

// try get trigger event of given type
SpellMapEventRec* MapUnit::GetTrigEvent(int type)
{
	for(auto &evt: trig_events)
		if(evt->evt_type == type)
			return(evt);
	return(NULL);
}

// try get trigger event of given types list
SpellMapEventRec* MapUnit::GetTrigEvent(std::vector<int> types)
{
	for(auto& evt: trig_events)
	{
		auto type_id = std::find(types.begin(), types.end(), evt->evt_type);
		if(type_id != types.end())
			return(evt);
	}
	return(NULL);
}
