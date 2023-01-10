#ifdef _MSC_VER
#ifndef WIN32
#define WIN32
#endif
#endif

#include <cstdio>
#if defined( WIN32 )
#include <tchar.h>
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOCRYPT
#include <windows.h>
#endif

#include <string>
#include <map>
#include <cstdint>
#include <vector>
#include <exception>
#include <fstream>
#include <memory>
#include <set>
#include <streambuf>

#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/tokenizer.hpp>

#if defined( WIN32 )
#pragma warning(push)
#pragma warning(disable: 4275 4996)
#endif
#include <json/json.h>
#include <tinyxml2.h>
#if defined( WIN32 )
#pragma warning(pop)
#endif

#if defined( WIN32 )
#define PATHSEP "\\"
#else
#define MAX_PATH 1024
#define PATHSEP "/"
#define printf_s printf
#define sprintf_s snprintf
#define _strcmpi strcasecmp
#define _stricmp strcasecmp
#endif

using std::string;
using std::vector;
using std::set;
using std::runtime_error;
using std::ofstream;
using std::ifstream;

using StringVector = vector<string>;
using StringSet = set<string>;

using NameToIDMapping = std::map<string, size_t>;

NameToIDMapping g_unitMapping;
NameToIDMapping g_abilityMapping;
NameToIDMapping g_upgradeMapping;

using AliasMap = std::map<string, set<string>>;

AliasMap g_aliases;

inline StringSet resolveAlias( string alias )
{
  if ( g_aliases.find( alias ) == g_aliases.end() )
  {
    StringSet set;
    set.insert( alias );
    return set;
  }
  return g_aliases[alias];
}

enum Race {
  Race_Neutral,
  Race_Terran,
  Race_Protoss,
  Race_Zerg
};

enum ResourceType {
  Resource_None,
  Resource_Minerals,
  Resource_Vespene,
  Resource_Terrazine,
  Resource_Custom
};

struct UnitAbilityCard {
  string name;
  std::map<uint64_t, string> commands;
  bool removed = false;
  size_t indexCtr = 0;
};

struct Unit {
  string name;
  string selectAlias;
  Race race = Race_Neutral;
  double lifeStart = 0.0;
  double lifeMax = 0.0;
  double speed = 0.0;
  double acceleration = 0.0;
  double food = 0.0;
  bool light = false;
  bool biological = false;
  bool mechanical = false;
  bool armored = false;
  bool structure = false;
  bool psionic = false;
  bool massive = false;
  double sight = 0.0;
  int64_t cargoSize = 0;
  double turningRate = 0.0;
  double shieldsStart = 0.0;
  double shieldsMax = 0.0;
  double lifeRegenRate = 0.0;
  double radius = 0.0;
  int64_t lifeArmor = 0;
  double speedMultiplierCreep = 1.0;
  int64_t mineralCost =  0;
  int64_t vespeneCost = 0;
  bool campaign = false;
  // StringSet abilityCommands;
  //std::map<uint64_t, string> abilityCommandsMap;
  std::map<size_t, UnitAbilityCard> abilityCardsMap{};
  std::map<size_t, size_t> abilityRowCo{};
  string mover;
  double shieldRegenDelay = 0.0;
  double shieldRegenRate = 0.0;
  StringSet weapons{};
  int64_t scoreMake = 0;
  int64_t scoreKill = 0;
  ResourceType resourceType = Resource_None;
  set<string> collides{};
  string aiEvaluateAlias;
  double aiEvalFactor = 0.0;
  double attackTargetPriority = 0.0;
  double stationaryTurningRate = 0.0;
  double lateralAcceleration = 0.0;
  set<string> planeArray{};
  bool invulnerable = false;
  bool resourceHarvestable = false; // vs. Raw (geyser without extractor)
  set<string> techAliases{};
  string glossaryAlias;
  string footprint;
  double energyStart = 0.0;
  double energyMax = 0.0;
  double energyRegenRate = 0.0;
  StringSet abilities {};
  std::map<size_t, string> behaviors {};
};

struct Upgrade {
  string name;
  Race race;
};

using UpgradeMap = std::map<string, Upgrade>;

enum AbilType {
  AbilType_Train,
  AbilType_Morph,
  AbilType_Build,
  AbilType_Merge, // archon
  AbilType_Research,
  AbilType_MorphPlacement, // spine crawler
  AbilType_EffectTarget, // inject larva
  AbilType_EffectInstant, // stimpack
  AbilType_Behavior, // banshee cloak
  AbilType_Other
};

struct AbilityCommand {
  string index;
  double time = 0.0;
  double energyCost = 0.0;
  double delay = 0.0;
  StringVector units{};
  string requirements;
  bool isUpgrade = false;
  string upgrade; // upgrade name
  int64_t mineralCost = 0; // for upgrade
  int64_t vespeneCost = 0; // for upgrade
  AbilityCommand()
  {
  }
  AbilityCommand( const string& idx ): index( idx ) {}
};

using AbilityCommandMap = std::map<string, AbilityCommand>;

struct Ability {
  string name;
  AbilType type = AbilType_Other;
  AbilityCommandMap commands;
  string morphUnit;
  string effect;
  std::map<string, double> vitalCosts; // only(?) applicable for effects and behaviors (vitals: "Life", "Energy")
  double range = 0.0; // only(?) applicable for effects
  double arc = 0.0; // only(?) applicable for effects
  bool warp = false;
  bool buildFinishKillsPeon = false;
  bool buildInterruptible = false;
  bool trainFinishKills = false;
  bool trainCancelKills = false;
};

using AbilityMap = std::map<string, Ability>;

using UnitMap = std::map<string, Unit>;
using UnitVector = std::vector<Unit>;

enum FilterAttribute {
  Search_Ground,
  Search_Structure,
  Search_Self,
  Search_Player,
  Search_Ally,
  Search_Air,
  Search_Stasis
};

void parseFilters( string full, std::set<FilterAttribute>& requires, std::set<FilterAttribute>& excludez )
{
  string excludes;
  auto split = full.find( ";" );
  if ( split != string::npos )
  {
    excludes = full.substr( split + 1 );
    full.erase( split, string::npos );
  }
  boost::char_separator<char> sep( "," );
  boost::tokenizer<boost::char_separator<char>> requireTokens( full, sep );
  boost::tokenizer<boost::char_separator<char>> excludeTokens( excludes, sep );
  auto func = []( boost::tokenizer<boost::char_separator<char>>& tokens, std::set<FilterAttribute>& st )
  {
    for ( const auto& tk : tokens )
    {
      if ( boost::iequals( tk, "Ground" ) )
        st.insert( Search_Ground );
      else if ( boost::iequals( tk, "Structure" ) )
        st.insert( Search_Structure );
      else if ( boost::iequals( tk, "Self" ) )
        st.insert( Search_Self );
      else if ( boost::iequals( tk, "Player" ) )
        st.insert( Search_Player );
      else if ( boost::iequals( tk, "Ally" ) )
        st.insert( Search_Ally );
      else if ( boost::iequals( tk, "Air" ) )
        st.insert( Search_Air );
      else if ( boost::iequals( tk, "Stasis" ) )
        st.insert( Search_Stasis );
    }
  };
  func( requireTokens, requires );
  func( excludeTokens, excludez );
}

inline Race raceToEnum( const char* str )
{
  if ( _strcmpi( str, "Terr" ) == 0 )
    return Race_Terran;
  else if ( _strcmpi( str, "Prot" ) == 0 )
    return Race_Protoss;
  else if ( _strcmpi( str, "Zerg" ) == 0 )
    return Race_Zerg;
  else
    return Race_Neutral;
}

inline bool boolValue( tinyxml2::XMLElement* element )
{
  return ( element->IntAttribute( "value" ) > 0 ? true : false );
}

inline ResourceType resourceToEnum( const char* str )
{
  if ( _strcmpi( str, "Minerals" ) == 0 )
    return Resource_Minerals;
  else if ( _strcmpi( str, "Vespene" ) == 0 )
    return Resource_Vespene;
  else if ( _strcmpi( str, "Terrazine" ) == 0 )
    return Resource_Terrazine;
  else if ( _strcmpi( str, "Custom" ) == 0 )
    return Resource_Custom;
  else
    return Resource_None;
}

inline uint64_t encodePoint( uint64_t page, uint64_t index )
{
  return ( page << 32 ) | ( index );
};

struct Weapon {
  string name;
  double range = 0.0;
  double period = 0.0; // time between attacks
  string effect; // name of effect
  double arc = 0.0;
  double damagePoint = 0.0;
  double backSwing = 0.0;
  double rangeSlop = 0.0;
  double arcSlop = 0.0;
  double minScanRange = 0.0;
  double randomDelayMin = 0.0;
  double randomDelayMax = 0.0;
  bool melee = false;
  bool hidden = false;
  bool disabled = false;
  bool suicide = false;
  std::set<FilterAttribute> targetRequire {};
  std::set<FilterAttribute> targetExclude {};
};

using WeaponMap = std::map<string, Weapon>;

void parseWeaponData( const string& filename, WeaponMap& weapons, Weapon& defaultWeapon )
{
  tinyxml2::XMLDocument doc;
  if ( doc.LoadFile( filename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw runtime_error( "Could not load XML file " + filename );

  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement();
  while ( entry )
  {
    bool isDefault = ( entry->Attribute( "default" ) && entry->Int64Attribute( "default" ) == 1 && !entry->Attribute( "id" ) );
    auto id = entry->Attribute( "id" );
    if ( id || isDefault )
    {
      if ( !isDefault && weapons.find( id ) == weapons.end() )
        weapons[id] = defaultWeapon;

      Weapon& wpn = ( isDefault ? defaultWeapon : weapons[id] );
      if ( !isDefault )
      {
        wpn.name = id;
        printf_s( "[+] weapon: %s\r\n", wpn.name.c_str() );
      }

      auto field = entry->FirstChildElement();
      while ( field )
      {
        if ( _strcmpi( field->Name(), "Range" ) == 0 )
          wpn.range = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "Period" ) == 0 )
          wpn.period = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "Arc" ) == 0 )
          wpn.arc = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "DamagePoint" ) == 0 )
          wpn.damagePoint = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "BackSwing" ) == 0 )
          wpn.backSwing = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "RangeSlop" ) == 0 )
          wpn.rangeSlop = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "ArcSlop" ) == 0 )
          wpn.arcSlop = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "MinScanRange" ) == 0 )
          wpn.minScanRange = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "RandomDelayMin" ) == 0 )
          wpn.randomDelayMin = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "RandomDelayMax" ) == 0 )
          wpn.randomDelayMax = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "TargetFilters" ) == 0 && field->Attribute( "value" ) )
        {
          string full = field->Attribute( "value" );
          parseFilters( full, wpn.targetRequire, wpn.targetExclude );
        }
        else if ( _strcmpi( field->Name(), "Options" ) == 0 )
        {
          if ( field->Attribute( "index" ) && field->Attribute( "value" ) )
          {
            if ( _stricmp( field->Attribute( "index" ), "Melee" ) == 0 )
              wpn.melee = boolValue( field );
            else if ( _stricmp( field->Attribute( "index" ), "Hidden" ) == 0 )
              wpn.hidden = boolValue( field );
            else if ( _stricmp( field->Attribute( "index" ), "Disabled" ) == 0 )
              wpn.disabled = boolValue( field );
          }
        }
        else if ( _strcmpi( field->Name(), "Effect" ) == 0 && field->Attribute( "value" ) )
          wpn.effect = field->Attribute( "value" );
        field = field->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement();
  }
}

struct EffectBonus {
  double value = 0.0;
};

struct EffectSplash {
  double radius = 0.0;
  double fraction = 0.0;
  string enumAreaEffect;
};

struct Effect {
  enum Type {
    Effect_Missile,
    Effect_Damage,
    Effect_CreateUnit,
    Effect_CreateHealer,
    Effect_Set,
    // Effect_Suicide, this is figured out for json export in resolveEffect()
    Effect_Persistent, // like lurker's spike
    Effect_EnumArea,
    Effect_Other
  } type;
  enum ImpactLocation {
    Impact_Undefined,
    Impact_SourceUnit,
    Impact_TargetPoint,
    Impact_TargetUnitOrPoint,
    Impact_TargetUnit,
    Impact_CasterUnit,
    Impact_CasterPoint
  };
  string name;
  string impactEffect; // for missile
  std::map<string, EffectBonus> attributeBonuses;
  std::map<size_t, EffectSplash> splashArea;
  double damageAmount = 0.0;
  double damageArmorReduction = 0.0;
  string damageKind;
  ImpactLocation impactLocation = Impact_Undefined;
  bool flagKill = false;
  std::map<size_t, string> setSubEffects;
  std::set<FilterAttribute> searchRequires;
  std::set<FilterAttribute> searchExcludes;
  std::set<string> persistentEffects;
  std::vector<double> persistentPeriods;
  size_t periodCount = 0;
};

using EffectMap = std::map<string, Effect>;

void parseEffectData( const string& filename, EffectMap& effects, size_t& notFoundCount )
{
  notFoundCount = 0;

  string effstr;

  ifstream in;
  in.open( filename, std::ifstream::in );
  if ( !in.is_open() )
    throw runtime_error( string( "Could not load XML file " ) + filename );
  in.seekg( 0, std::ios::end );
  effstr.reserve( in.tellg() );
  in.seekg( 0, std::ios::beg );
  effstr.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
  in.close();

  boost::replace_all( effstr, R"(<?token id="abil" type="CAbilLink"?>)", "" );
  boost::replace_all( effstr, R"(<?token id="abil"?>)", "" );
  boost::replace_all( effstr, R"(<?token id="n" type="uint32"?>)", "" );
  boost::replace_all( effstr, R"(<?token id="nNext" type="uint32"?>)", "" );

  tinyxml2::XMLDocument doc;
  auto ret = doc.Parse( effstr.c_str() );
  if ( ret != tinyxml2::XML_SUCCESS )
  {
    doc.PrintError();
    throw runtime_error( "Could not parse XML file" );
  }

  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement();
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      if ( entry->Attribute( "parent" ) && strlen( entry->Attribute( "parent" ) ) > 0 )
      {
        string parentId = entry->Attribute( "parent" );
        if ( effects.find( parentId ) == effects.end() )
        {
          notFoundCount++;
          entry = entry->NextSiblingElement( );
          continue;
        }
        else if ( effects.find( id ) == effects.end() )
        {
          const Effect& parent = effects[parentId];
          effects[id] = parent;
        }
      }

      Effect& effect = effects[id];
      effect.name = id;

      printf_s( "[+] effect: %s\r\n", effect.name.c_str() );

      if ( _stricmp( entry->Name(), "CEffectDamage" ) == 0 )
        effect.type = Effect::Effect_Damage;
      else if ( _stricmp( entry->Name(), "CEffectLaunchMissile" ) == 0 )
        effect.type = Effect::Effect_Missile;
      else if ( _stricmp( entry->Name(), "CEffectCreateUnit" ) == 0 )
        effect.type = Effect::Effect_CreateUnit;
      else if ( _stricmp( entry->Name(), "CEffectCreateHealer" ) == 0 )
        effect.type = Effect::Effect_CreateHealer;
      else if ( _stricmp( entry->Name(), "CEffectSet" ) == 0 )
        effect.type = Effect::Effect_Set;
      else if ( _stricmp( entry->Name(), "CEffectCreatePersistent" ) == 0 )
        effect.type = Effect::Effect_Persistent;
      else if ( _stricmp( entry->Name(), "CEffectEnumArea" ) == 0 )
        effect.type = Effect::Effect_EnumArea;
      else
        effect.type = Effect::Effect_Other;

      size_t areaArrayCtr = 0;
      size_t fxArrayCtr = 0;
      auto field = entry->FirstChildElement();
      while ( field )
      {
        if ( _strcmpi( field->Name(), "ArmorReduction" ) == 0 )
          effect.damageArmorReduction = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "Amount" ) == 0 )
          effect.damageAmount = field->DoubleAttribute( "value" );
        else if ( _strcmpi( field->Name(), "ImpactEffect" ) == 0 )
          effect.impactEffect = field->Attribute( "value" );
        else if ( _strcmpi( field->Name(), "Kind" ) == 0 )
          effect.damageKind = field->Attribute( "value" );
        else if ( _strcmpi( field->Name(), "Flags" ) == 0 && field->Attribute( "index" ) && field->Attribute( "value" ) )
        {
          if ( boost::iequals( field->Attribute( "index" ), "Kill" ) )
            effect.flagKill = field->IntAttribute( "value" ) > 0;
        }
        else if ( _strcmpi( field->Name(), "ImpactLocation" ) == 0 && field->Attribute( "Value" ) )
        {
          if ( boost::iequals( field->Attribute( "Value" ), "SourceUnit" ) )
            effect.impactLocation = Effect::Impact_SourceUnit;
          else if ( boost::iequals( field->Attribute( "Value" ), "TargetPoint" ) )
            effect.impactLocation = Effect::Impact_TargetPoint;
          else if ( boost::iequals( field->Attribute( "Value" ), "TargetUnitOrPoint" ) )
            effect.impactLocation = Effect::Impact_TargetUnitOrPoint;
          else if ( boost::iequals( field->Attribute( "Value" ), "TargetUnit" ) )
            effect.impactLocation = Effect::Impact_TargetUnit;
          else if ( boost::iequals( field->Attribute( "Value" ), "CasterPoint" ) )
            effect.impactLocation = Effect::Impact_CasterPoint;
          else if ( boost::iequals( field->Attribute( "Value" ), "CasterUnit" ) )
            effect.impactLocation = Effect::Impact_CasterUnit;
        }
        else if ( _strcmpi( field->Name(), "SearchFilters" ) == 0 && field->Attribute( "value" ) )
        {
          string full = field->Attribute( "value" );
          parseFilters( full, effect.searchRequires, effect.searchExcludes );
        }
        else if ( _strcmpi( field->Name(), "AreaArray" ) == 0 )
        {
          areaArrayCtr = ( field->Attribute( "index" ) ? field->UnsignedAttribute( "index" ) : areaArrayCtr );
          auto& area = effect.splashArea[areaArrayCtr];
          if ( field->Attribute( "Radius" ) )
            area.radius = field->DoubleAttribute( "Radius" );
          if ( field->Attribute( "Fraction" ) )
            area.fraction = field->DoubleAttribute( "Fraction" );
          if ( field->Attribute( "Effect" ) )
            area.enumAreaEffect = field->Attribute( "Effect" );
          areaArrayCtr++;
        }
        // Array of effects, usually one, that will run after each period.
        else if ( boost::iequals( field->Name(), "PeriodicEffectArray" ) && field->Attribute( "value" ) )
        {
          effect.persistentEffects.insert( field->Attribute( "value" ) );
        }
        // Array of waiting periods between each effect of this periodic set. If less than PeriodCount, loops around.
        else if ( boost::iequals( field->Name(), "PeriodicPeriodArray" ) && field->Attribute( "value" ) )
        {
          effect.persistentPeriods.push_back( field->DoubleAttribute( "value" ) );
        }
        // Count of periods this effect lasts. If not specified, using the size of PeriodicPeriodArray (?)
        else if ( boost::iequals( field->Name(), "PeriodCount" ) && field->Attribute( "value" ) )
        {
          effect.periodCount = field->UnsignedAttribute( "value" );
        }
        else if ( _strcmpi( field->Name(), "AttributeBonus" ) == 0 && field->Attribute( "index" ) )
        {
          if ( field->Attribute( "value" ) )
            effect.attributeBonuses[field->Attribute( "index" )].value = field->DoubleAttribute( "value" );
        }
        else if ( _strcmpi( field->Name(), "EffectArray" ) == 0 )
        {
          fxArrayCtr = ( field->Attribute( "index" ) ? field->UnsignedAttribute( "index" ) : fxArrayCtr );
          if ( field->Attribute( "value" ) )
            effect.setSubEffects[fxArrayCtr] = field->Attribute( "value" );
          fxArrayCtr++;
        }
        field = field->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement();
  }
}

inline size_t encodeRowCol( int row, int col )
{
  return ( ( (size_t)row * 100 ) + (size_t)col );
}

void parseUnitData( const string& filename, UnitMap& units, Unit& defaultUnit, size_t& notFoundCount )
{
  notFoundCount = 0;

  tinyxml2::XMLDocument doc;
  if ( doc.LoadFile( filename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw runtime_error( string( "Could not load XML file " ) + filename );

  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement( "CUnit" );
  while ( entry )
  {
    bool isDefault = ( entry->Attribute( "default" ) && entry->Int64Attribute( "default" ) == 1 && !entry->Attribute( "id" ) );
    auto id = entry->Attribute( "id" );
    if ( id || isDefault )
    {
      if ( entry->Attribute( "parent" ) && strlen( entry->Attribute( "parent" ) ) > 0 )
      {
        string parentId = entry->Attribute( "parent" );
        if ( units.find( parentId ) == units.end() )
        {
          printf( "Skipping unit %s because it has a parent that has not been parsed yet\r\n", id );
          notFoundCount++;
          entry = entry->NextSiblingElement( "CUnit" );
          continue;
        }
        else if ( units.find( id ) == units.end() )
        {
          // copy base data from parent before parsing this descendant
          // if it does not exist yet
          printf( "Copying unit %s from parent %s because it does not exist yet\r\n", id, parentId.c_str() );
          const Unit& parent = units[parentId];
          units[id] = parent;
        }
      }

      if ( !isDefault && units.find( id ) == units.end() )
        units[id] = defaultUnit;

      Unit& unit = ( isDefault ? defaultUnit : units[id] );

      if ( !isDefault ) {
        unit.name = id;
        printf_s( "[+] unit: %s\r\n", unit.name.c_str() );
      }

      size_t cardctr = 0; // index of current CardLayouts subitem
      size_t behaviorctr = 0;
      auto field = entry->FirstChildElement();
      while ( field )
      {
        if ( _strcmpi( field->Name(), "Race" ) == 0 )
          unit.race = raceToEnum( field->Attribute( "value" ) );
        else if ( _stricmp( field->Name(), "LifeStart" ) == 0 )
          unit.lifeStart = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "LifeMax" ) == 0 )
          unit.lifeMax = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Speed" ) == 0 )
          unit.speed = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Acceleration" ) == 0 )
          unit.acceleration = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Food" ) == 0 )
          unit.food = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Sight" ) == 0 )
          unit.sight = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "ScoreMake" ) == 0 )
          unit.scoreMake = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "ScoreKill" ) == 0 )
          unit.scoreKill = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "AIEvaluateAlias" ) == 0 )
        {
          unit.aiEvaluateAlias = field->Attribute( "value" );
          if ( unit.aiEvaluateAlias == "##id##" )
            unit.aiEvaluateAlias = unit.name;
        }
        else if ( _stricmp( field->Name(), "AttackTargetPriority" ) == 0 )
          unit.attackTargetPriority = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "StationaryTurningRate" ) == 0 )
          unit.stationaryTurningRate = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "LateralAcceleration" ) == 0 )
          unit.lateralAcceleration = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "AIEvalFactor" ) == 0 )
          unit.aiEvalFactor = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Attributes" ) == 0 )
        {
          if ( _stricmp( field->Attribute( "index" ), "Light" ) == 0 )
            unit.light = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Biological" ) == 0 )
            unit.biological = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Mechanical" ) == 0 )
            unit.mechanical = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Armored" ) == 0 )
            unit.armored = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Structure" ) == 0 )
            unit.structure = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Psionic" ) == 0 )
            unit.psionic = boolValue( field );
          else if ( _stricmp( field->Attribute( "index" ), "Massive" ) == 0 )
            unit.massive = boolValue( field );
        }
        else if ( _stricmp( field->Name(), "ResourceType" ) == 0 )
          unit.resourceType = resourceToEnum( field->Attribute( "value" ) );
        else if ( _stricmp( field->Name(), "ResourceState" ) == 0 && field->Attribute( "value" ) )
          unit.resourceHarvestable = ( _stricmp( field->Attribute( "value" ), "Harvestable" ) == 0 );
        else if ( _stricmp( field->Name(), "CargoSize" ) == 0 )
          unit.cargoSize = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "ShieldsStart" ) == 0 )
          unit.shieldsStart = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "ShieldsMax" ) == 0 )
          unit.shieldsMax = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "TurningRate" ) == 0 )
          unit.turningRate = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "LifeRegenRate" ) == 0 )
          unit.lifeRegenRate = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "EnergyStart" ) == 0 )
          unit.energyStart = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "EnergyMax" ) == 0 )
          unit.energyMax = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "EnergyRegenRate" ) == 0 )
          unit.energyRegenRate = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "Radius" ) == 0 )
          unit.radius = field->DoubleAttribute( "value" );
        else if ( _stricmp( field->Name(), "LifeArmor" ) == 0 )
          unit.lifeArmor = field->Int64Attribute( "value" );
        else if ( _stricmp( field->Name(), "SpeedMultiplierCreep" ) == 0 )
          unit.speedMultiplierCreep = field->DoubleAttribute("value");
        else if ( _stricmp( field->Name(), "SelectAlias" ) == 0 )
        {
          unit.selectAlias = field->Attribute( "value" );
          if ( unit.selectAlias == "##id##" )
            unit.selectAlias = unit.name;
        }
        else if ( _stricmp( field->Name(), "CostResource" ) == 0 )
        {
          if ( _stricmp( field->Attribute( "index" ), "Minerals" ) == 0 )
            unit.mineralCost = field->Int64Attribute( "value" );
          else if ( _stricmp( field->Attribute( "index" ), "Vespene" ) == 0 )
            unit.vespeneCost = field->Int64Attribute( "value" );
        }
        else if ( _stricmp( field->Name(), "BehaviorArray" ) == 0 )
        {
          size_t entidx = ( field->Attribute( "index" ) ? field->UnsignedAttribute( "index" ) : behaviorctr );
          auto& ent = unit.behaviors[entidx];
          if ( field->Attribute( "Link" ) )
            ent = field->Attribute( "Link" );
          if ( field->Attribute( "removed" ) && field->Int64Attribute( "removed" ) > 0 )
            ent = "";
          behaviorctr++;
        }
        else if ( _stricmp( field->Name(), "CardLayouts" ) == 0 )
        {
          size_t cardidx = ( field->Attribute( "index" ) ? field->UnsignedAttribute( "index" ) : cardctr );
          auto& card = unit.abilityCardsMap[cardidx];
          if ( field->Attribute( "CardId" ) )
            card.name = field->Attribute( "CardId" );
          if ( field->Attribute( "removed" ) && field->Int64Attribute( "removed" ) > 0 )
            card.removed = true;
          else
            card.removed = false;
          auto sub = field->FirstChildElement( "LayoutButtons" );
          size_t ctr = card.indexCtr; // ctr = 0;
          while ( sub )
          {
            /*if ( boost::iequals( unit.name, "Baneling" ) && sub->Attribute( "AbilCmd" ) && _stricmp(  sub->Attribute("AbilCmd"), "BurrowBanelingDown,Execute" ) == 0 )
            {
              char asd[512];
              sprintf_s( asd, 512, "Baneling ability: %s\r\n", sub->Attribute( "AbilCmd" ) );
              OutputDebugStringA( asd );
              printf( asd );
            }*/
            int64_t originalCtr = ctr;
            bool overriding = sub->Attribute( "index" ) ? true : false;
            if ( overriding )
            {
              ctr = sub->Int64Attribute( "index" );
              auto itr = unit.abilityRowCo.begin();
              while ( itr != unit.abilityRowCo.end() )
              {
                if ( ( *itr ).second == ctr )
                  itr = unit.abilityRowCo.erase( itr );
                else
                  itr++;
              }
            }
            if ( ( sub->Attribute( "removed" ) && sub->Int64Attribute( "removed" ) > 0 ) || ( sub->Attribute( "Type" ) && _stricmp( sub->Attribute( "Type" ), "Undefined" ) == 0 ) )
              card.commands.erase( ctr );
            else if ( sub->Attribute( "AbilCmd" ) )
            {
              //if (unit.name == "Infestor")
              //  OutputDebugStringA("dbg");
              auto abilface = string( sub->Attribute( "Face" ) ? sub->Attribute( "Face" ) : "" );
              auto abilcmd = string( sub->Attribute( "AbilCmd" ));
              // for whatever reason, current voidmulti unitdata has *all* burrow up/down commands
              // on every zerg ground unit's command card. so this hack avoids that mess
              // also avoid matching InfestorTerran entirely, which has been removed, but due to containing "Infestor" would match normal infestor burrow as well
              auto pt1 = (abilface.find("Burrow") == string::npos);
              auto pt2 = (abilface.find("Burrow") == 0);
              auto pt3 = (abilcmd.find(unit.selectAlias.empty() ? unit.aiEvaluateAlias.empty() ? unit.name : unit.aiEvaluateAlias : unit.selectAlias) != string::npos);
              auto pt4 = (abilcmd.find("Terran") == string::npos);
              if ( _stricmp( abilface.c_str(), "ResearchBurrow" ) == 0 || ( pt1 || ( pt2 && pt3 && pt4 ) ) )
              {
                card.commands[ctr] = abilcmd;
                if ( sub->Attribute( "Row" ) && sub->Attribute( "Column" ) )
                {
                  auto row = sub->IntAttribute( "Row" );
                  auto col = sub->IntAttribute( "Column" );
                  unit.abilityRowCo[encodeRowCol( row, col )] = ctr;
                }
              }
            }

            sub = sub->NextSiblingElement( "LayoutButtons" );
            ctr = ( overriding ? originalCtr : ctr + 1 );
          }
          card.indexCtr = ctr;
          cardctr++;
        }
        else if ( _strcmpi( field->Name(), "TechAliasArray" ) == 0 && field->Attribute( "value" ) )
        {
          g_aliases[field->Attribute( "value" )].insert( unit.name );
          unit.techAliases.insert( field->Attribute( "value" ) );
        }
        else if ( _stricmp( field->Name(), "Mover" ) == 0 && field->Attribute( "value" ) )
          unit.mover = field->Attribute( "value" );
        else if ( _stricmp( field->Name(), "GlossaryAlias" ) == 0 && field->Attribute( "value" ) )
          unit.glossaryAlias = field->Attribute( "value" );
        else if ( _stricmp( field->Name(), "ShieldRegenDelay" ) == 0 && field->Attribute( "value" ) )
          unit.shieldRegenDelay = static_cast<double>( field->Int64Attribute( "value" ) );
        else if ( _stricmp( field->Name(), "ShieldRegenRate" ) == 0 && field->Attribute( "value" ) )
          unit.shieldRegenRate = static_cast<double>( field->Int64Attribute( "value" ) );
        else if ( _stricmp( field->Name(), "WeaponArray" ) == 0 && field->Attribute( "Link" ) )
          unit.weapons.insert( field->Attribute( "Link" ) );
        else if ( _strcmpi( field->Name(), "FlagArray" ) == 0 && field->Attribute( "index" ) && field->Attribute( "value" ) )
        {
          if ( _stricmp( field->Attribute( "index" ), "Invulnerable" ) == 0 )
            unit.invulnerable = ( field->IntAttribute( "value" ) > 0 ? true : false );
        }
        else if ( _stricmp( field->Name(), "Footprint" ) == 0 && field->Attribute( "value" ) )
          unit.footprint = field->Attribute( "value" );
        else if ( _stricmp( field->Name(), "EditorCategories" ) == 0 && field->Attribute( "value" ) )
        {
          string editorCategories = field->Attribute( "value" );
          if ( editorCategories.find( "ObjectFamily:Campaign" ) != string::npos )
            unit.campaign = true;
        }
        else if ( _strcmpi( field->Name(), "PlaneArray" ) == 0 && field->Attribute( "index" ) )
        {
          if ( field->Attribute( "value" ) && field->IntAttribute( "value" ) > 0 )
            unit.planeArray.insert( field->Attribute( "index" ) );
          else if ( field->Attribute( "removed" ) || ( field->Attribute( "value" ) && field->IntAttribute( "value" ) < 1 ) )
            unit.planeArray.erase( field->Attribute( "index" ) );
        }
        else if ( _strcmpi( field->Name(), "Collide" ) == 0 && field->Attribute( "index" ) )
        {
          if ( field->Attribute( "value" ) && field->IntAttribute( "value" ) > 0 )
            unit.collides.insert( field->Attribute( "index" ) );
          else if ( field->Attribute( "removed" ) || ( field->Attribute( "value" ) && field->IntAttribute( "value" ) < 1 ) )
            unit.collides.erase( field->Attribute( "index" ) );
        }
        else if ( _stricmp( field->Name(), "AbilArray" ) == 0 && field->Attribute( "Link" ) )
          unit.abilities.insert( field->Attribute( "Link" ) );

        field = field->NextSiblingElement();
      }
      /*if ( boost::iequals( unit.name, "Baneling" ) )
      {
        DebugBreak();
      }*/
    }
    entry = entry->NextSiblingElement( "CUnit" );
  }
}

struct Requirement {
  string id;
  string useNodeName;
  string showNodeName;
  Requirement( const string& id_ = "" ): id( id_ ) {}
};

using RequirementMap = std::map<string, Requirement>;

enum RequirementNodeType {
  ReqNode_Unknown,
  ReqNode_CountUpgrade,
  ReqNode_CountUnit,
  ReqNode_LogicAnd,
  ReqNode_LogicOr,
  ReqNode_LogicEq,
  ReqNode_LogicNot
};

inline RequirementNodeType reqNodeTypeToEnum( const char* str )
{
  if ( _strcmpi( str, "CRequirementCountUpgrade" ) == 0 )
    return ReqNode_CountUpgrade;
  else if ( _strcmpi( str, "CRequirementCountUnit" ) == 0 )
    return ReqNode_CountUnit;
  else if ( _strcmpi( str, "CRequirementAnd" ) == 0 )
    return ReqNode_LogicAnd;
  else if ( _strcmpi( str, "CRequirementOr" ) == 0 )
    return ReqNode_LogicOr;
  else if ( _strcmpi( str, "CRequirementEq" ) == 0 )
    return ReqNode_LogicEq;
  else if ( _strcmpi( str, "CRequirementNot" ) == 0 )
    return ReqNode_LogicNot;
  else
    return ReqNode_Unknown;
}

struct RequirementNode {
  string id;
  RequirementNodeType type = ReqNode_Unknown;
  string countLink; // countUpgrade, countUnit
  string countState; // countUpgrade, countUnit
  std::map<size_t, string> operands {}; // and, or, eq, not
  RequirementNode( const string& id_ = "" ): id( id_ ) {}
};

using RequirementNodeMap = std::map<string, RequirementNode>;

void parseRequirementData( const string& datafilename, const string& nodedatafilename, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  tinyxml2::XMLDocument datadoc;
  if ( datadoc.LoadFile( datafilename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw runtime_error( "Could not load RequirementData XML file" );

  tinyxml2::XMLDocument nodedatadoc;
  if ( nodedatadoc.LoadFile( nodedatafilename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw runtime_error( "Could not load RequirementNodeData XML file" );

  // RequirementData.xml
  auto catalog = datadoc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement( "CRequirement" );
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      if ( requirements.find( id ) == requirements.end() )
        requirements[id] = Requirement( id );

      Requirement& requirement = requirements[id];
      auto child = entry->FirstChildElement();
      while ( child )
      {
        if ( _stricmp( child->Name(), "NodeArray" ) == 0 && child->Attribute( "index" ) && child->Attribute( "Link" ) )
        {
          if ( _stricmp( child->Attribute( "index" ), "Use" ) == 0 )
            requirement.useNodeName = child->Attribute( "Link" );
          if ( _stricmp( child->Attribute( "index" ), "Show" ) == 0 )
            requirement.showNodeName = child->Attribute( "Link" );
        }
        child = child->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement( "CRequirement" );
  }

  // RequirementNodeData.xml
  catalog = nodedatadoc.FirstChildElement( "Catalog" );
  entry = catalog->FirstChildElement();
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      if ( nodes.find( id ) == nodes.end() )
        nodes[id] = RequirementNode( id );

      RequirementNode& node = nodes[id];
      node.type = reqNodeTypeToEnum( entry->Name() );
      if ( node.type == ReqNode_LogicAnd || node.type == ReqNode_LogicOr || node.type == ReqNode_LogicEq || node.type == ReqNode_LogicNot )
        node.operands.clear();

      auto child = entry->FirstChildElement();
      while ( child )
      {
        if ( _stricmp( child->Name(), "Count" ) == 0 )
        {
          if ( child->Attribute( "Link" ) )
            node.countLink = child->Attribute( "Link" );
          if ( child->Attribute( "State" ) )
            node.countState = child->Attribute( "State" );
        }
        else if ( _stricmp( child->Name(), "OperandArray" ) == 0 && child->Attribute( "value" ) )
        {
          auto opIndex = child->UnsignedAttribute( "index", static_cast<unsigned int>( node.operands.size() ) );
          node.operands[opIndex] = child->Attribute( "value" );
        }
        child = child->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement();
  }
}

struct OffsetPoint {
  double x, y;
  OffsetPoint( double x_, double y_ ): x( x_ ), y( y_ ) {}
};

struct FootprintShapeBorder {
  size_t offset1;
  size_t offset2;
  int type;
  FootprintShapeBorder( unsigned long o1, unsigned long o2, int t_ ): offset1( o1 ), offset2( o2 ), type( t_ ) {}
};

using FootprintShapeBucket = vector<size_t>;
using FootprintShapeBucketVector = vector<FootprintShapeBucket>;

struct Point2DI {
  int x, y;
  Point2DI( int x_, int y_ ): x( x_ ), y( y_ ) {}
};

using Polygon = vector<Point2DI>;
using PolygonVector = vector<Polygon>;

struct FootprintShape {
  double radius = 0.0;
  PolygonVector unpathablePolys {};
  PolygonVector buildingPolys {};
};

struct Footprint {
  string id;
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  bool removed = false;
  bool hasCreep = false;
  bool hasNearResources = false;
  char creepChar{};
  char nearResourcesChar{};
  vector<char> placement {};
  vector<char> creep {};
  vector<char> nearResources {};
  FootprintShape shape;
};

using FootprintMap = std::map<string, Footprint>;

void parseFootprintShapes( vector<OffsetPoint>& offsets, vector<FootprintShapeBorder>& borders, int type, PolygonVector& out )
{
  out.clear();

  FootprintShapeBucketVector buckets;
  for ( auto& border : borders )
  {
    if ( border.type != type ) // 0 = unpathable terrain, 1 = ground, 2 = building, 3 = cliff
      continue;
    bool newBucket = true;
    for ( auto& bucket : buckets )
    {
      auto it2 = std::find( bucket.begin(), bucket.end(), border.offset2 );
      if ( it2 == bucket.begin() && !bucket.empty() )
      {
        FootprintShapeBucket prepend;
        for ( auto& srcBucket : buckets )
        {
          auto it1 = std::find( srcBucket.begin(), srcBucket.end(), border.offset1 );
          if ( it1 != srcBucket.end() && std::next( it1 ) == srcBucket.end() && &srcBucket != &bucket )
          {
            prepend.swap( srcBucket );
            break;
          }
        }
        if ( prepend.empty() )
        {
          bucket.insert( it2, border.offset1 );
        }
        else
        {
          for ( auto& roundRobin : buckets )
          {
            if ( !roundRobin.empty() && roundRobin.front() == border.offset2 && roundRobin.back() == prepend.front() )
            {
              roundRobin.insert( roundRobin.begin(), std::begin( prepend ), std::end( prepend ) );
              goto nextloop;
            }
          }
          bucket.insert( it2, std::begin( prepend ), std::end( prepend ) );
        }
        goto nextloop;
      }
    }
    if ( newBucket )
    {
      FootprintShapeBucket bucket;
      bucket.push_back( border.offset1 );
      bucket.push_back( border.offset2 );
      buckets.push_back( bucket );
    }
nextloop:;
  }
  FootprintShapeBucketVector outBuckets;
  while ( !buckets.empty() )
  {
    auto it = buckets.begin();
    while ( it != buckets.end() )
    {
      if ( ( *it ).empty() )
        it = buckets.erase( it );
      else if ( ( *it ).front() == ( *it ).back() )
      {
        outBuckets.push_back( ( *it ) );
        it = buckets.erase( it );
      } else
        it++;
    }
    it = buckets.begin();
    if ( it != buckets.end() )
    {
      for ( auto& bucket : buckets )
      {
        if ( bucket.size() < 2 )
          continue;
        if ( ( *it ).back() == bucket.front() && &( *it ) != &bucket )
        {
          ( *it ).insert( ( *it ).end(), std::next( bucket.begin() ), bucket.end() );
          bucket.clear();
          break;
        }
      }
    }
  }
  for ( auto& bucket : outBuckets )
  {
    Polygon poly;
    for ( auto idx : bucket )
    {
      poly.emplace_back( (int)( offsets[idx].x * 1000.0 ), (int)( offsets[idx].y * 1000.0 ) );
    }
    out.push_back( poly );
  }
}

void parseFootprintData( const string& filename, FootprintMap& footprints, Footprint& defaultFootprint, size_t& notFoundCount )
{
  notFoundCount = 0;

  tinyxml2::XMLDocument doc;
  if ( doc.LoadFile( filename.c_str() ) != tinyxml2::XML_SUCCESS )
    throw runtime_error( "Could not load FootprintData XML file" );

  // FootprintData.xml
  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement( "CFootprint" );
  while ( entry )
  {
    bool isDefault = ( entry->Attribute( "default" ) && entry->Int64Attribute( "default" ) == 1 && !entry->Attribute( "id" ) );
    auto id = entry->Attribute( "id" );
    if ( id || isDefault )
    {
      if ( entry->Attribute( "parent" ) && strlen( entry->Attribute( "parent" ) ) > 0 )
      {
        string parentId = entry->Attribute( "parent" );
        if ( footprints.find( parentId ) == footprints.end() )
        {
          notFoundCount++;
          entry = entry->NextSiblingElement();
          continue;
        }
        else if ( footprints.find( id ) == footprints.end() )
        {
          const Footprint& parent = footprints[parentId];
          footprints[id] = parent;
        }
      }
      else if ( !isDefault && footprints.find( id ) == footprints.end() )
        footprints[id] = defaultFootprint;

      Footprint& fp = ( isDefault ? defaultFootprint : footprints[id] );

      if ( !isDefault )
      {
        fp.id = id;
        printf_s( "[+] footprint: %s\r\n", fp.id.c_str() );
      }

      vector<OffsetPoint> offsets;
      vector<FootprintShapeBorder> borders;

      auto shape = entry->FirstChildElement( "Shape" );
      while ( shape )
      {
        auto field = shape->FirstChildElement();
        while ( field )
        {
          if ( _strcmpi( field->Name(), "Radius" ) == 0 && field->Attribute( "value" ) )
            fp.shape.radius = field->DoubleAttribute( "value" );
          else if ( _strcmpi( field->Name(), "Offsets" ) == 0 && field->Attribute( "value" ) )
          {
            offsets.clear();
            string data = field->Attribute( "value" );
            vector<string> offsetstrs;
            boost::split( offsetstrs, data, boost::is_any_of( ";" ) );
            for ( auto& offsetstr : offsetstrs )
            {
              vector<string> parts;
              boost::split( parts, offsetstr, boost::is_any_of( "," ) );
              if ( parts.size() != 2 )
                throw runtime_error( "CFootprint::Shape::Offsets has a non-pair entry" );
              offsets.emplace_back( std::stod( parts[0] ), std::stod( parts[1] ) );
            }
          }
          else if ( _strcmpi( field->Name(), "Borders" ) == 0 && field->Attribute( "value" ) )
          {
            borders.clear();
            string data = field->Attribute( "value" );
            vector<string> borderstrs;
            boost::split( borderstrs, data, boost::is_any_of( ";" ) );
            for ( auto& borderstr : borderstrs )
            {
              vector<string> parts;
              boost::split( parts, borderstr, boost::is_any_of( "," ) );
              if ( parts.size() != 3 )
                throw runtime_error( "CFootprint::Shape::Borders has an entry with != 3 members" );
              borders.emplace_back( std::stoul( parts[0] ), std::stoul( parts[1] ), std::stoi( parts[2] ) );
            }
          }
          field = field->NextSiblingElement();
        }
        shape = shape->NextSiblingElement( "Shape" );
      }

      // 0 = unpathable terrain, 1 = ground, 2 = building, 3 = cliff
      parseFootprintShapes( offsets, borders, 0, fp.shape.unpathablePolys );
      parseFootprintShapes( offsets, borders, 2, fp.shape.buildingPolys );

      auto layer = entry->FirstChildElement( "Layers" );
      while ( layer )
      {
        if ( layer->Attribute( "index" ) && boost::iequals( layer->Attribute( "index" ), "Place" ) )
        {
          if ( layer->IntAttribute( "removed", 0 ) == 1 )
          {
            fp.removed = true;
            layer = layer->NextSiblingElement( "Layers" );
            continue;
          }

          fp.removed = false;

          if ( layer->Attribute( "Area" ) )
          {
            string area = layer->Attribute( "Area" );
            vector<string> parts;
            boost::split( parts, area, boost::is_any_of( "," ) );
            if ( parts.size() != 4 )
              throw runtime_error( "bad Area attribute for CFootprint::Layers" );

            fp.x = atoi( parts[0].c_str() );
            fp.y = atoi( parts[1].c_str() );
            int rx = atoi( parts[2].c_str() );
            int ry = atoi( parts[3].c_str() );
            fp.w = ( rx - fp.x );
            fp.h = ( ry - fp.y );
          }

          auto sets = layer->FirstChildElement( "Sets" );
          while ( sets )
          {
            if ( sets->Attribute( "Character" ) && strlen( sets->Attribute( "Character" ) ) == 1 )
            {
              auto positive = sets->FirstChildElement( "Positive" );
              if ( positive && positive->Attribute( "index" ) && ( !positive->Attribute( "value" ) || positive->IntAttribute( "value" ) == 1 ) )
              {
                if ( boost::iequals( positive->Attribute( "index" ), "Creep" ) )
                {
                  fp.hasCreep = true;
                  fp.creepChar = sets->Attribute( "Character" )[0];
                }
                else if ( boost::iequals( positive->Attribute( "index" ), "NearResources" ) )
                {
                  fp.hasNearResources = true;
                  fp.nearResourcesChar = sets->Attribute( "Character" )[0];
                }
              }
            }
            sets = sets->NextSiblingElement( "Sets" );
          }

          if ( layer->FirstChildElement( "Rows" ) && ( !layer->FirstChildElement( "Rows" )->Attribute( "removed" ) || layer->FirstChildElement( "Rows" )->IntAttribute( "removed" ) == 0 ) )
          {
            // jesus christ
            int rowcount = 0;
            int rowwidth = 0;
            auto ctr = layer->FirstChildElement( "Rows" );
            while ( ctr )
            {
              size_t val = ( ctr->Attribute( "value" ) ? strlen( ctr->Attribute( "value" ) ) : 0 );
              if ( static_cast<int>( val ) > rowwidth )
                rowwidth = static_cast<int>( val );
              rowcount++;
              ctr = ctr->NextSiblingElement( "Rows" );
            }

            if ( rowcount > fp.h )
              fp.h = rowcount;
            if ( rowwidth > fp.w )
              fp.w = rowwidth;

            fp.placement.clear();
            fp.placement.resize( fp.w * fp.h, 0 );
            fp.creep.clear();
            fp.creep.resize( fp.w * fp.h, 0 );
            fp.nearResources.clear();
            fp.nearResources.resize( fp.w * fp.h, 0 );
            size_t index = 0;
            auto row = layer->FirstChildElement( "Rows" );
            while ( row )
            {
              if ( !row->Attribute( "value" ) )
                throw runtime_error( "CFootprint::Layers::Rows without value attribute" );
              string value = row->Attribute( "value" );
              if ( static_cast<int>( value.length() ) > fp.w )
                fp.w = static_cast<int>( value.length() );
              for ( size_t c = 0; c < value.length(); c++ )
                if ( value[c] == 'x' )
                  fp.placement[index * fp.w + c] = 1;
                else if ( fp.hasCreep && value[c] == fp.creepChar )
                  fp.creep[index * fp.w + c] = 1;
                else if ( fp.hasNearResources && value[c] == fp.nearResourcesChar )
                  fp.nearResources[index * fp.w + c] = 1;
              index++;
              row = row->NextSiblingElement( "Rows" );
            }
          }
        }
        layer = layer->NextSiblingElement( "Layers" );
      }
    }
    entry = entry->NextSiblingElement( "CFootprint" );
  }
}

void parseAbilityData( const string& filename, AbilityMap& abilities )
{
  string abilstr;

  ifstream in;
  in.open( filename, std::ifstream::in );
  if ( !in.is_open() )
    throw runtime_error( string( "Could not load XML file " ) + filename );
  in.seekg( 0, std::ios::end );
  abilstr.reserve( in.tellg() );
  in.seekg( 0, std::ios::beg );
  abilstr.assign( std::istreambuf_iterator<char>( in ), std::istreambuf_iterator<char>() );
  in.close();

  boost::replace_all( abilstr, R"(<?token id="unit"?>)", "" );

  tinyxml2::XMLDocument doc;
  auto ret = doc.Parse( abilstr.c_str() );
  if ( ret != tinyxml2::XML_SUCCESS )
  {
    doc.PrintError();
    throw runtime_error( "Could not parse XML file" );
  }

  auto catalog = doc.FirstChildElement( "Catalog" );
  auto entry = catalog->FirstChildElement();
  while ( entry )
  {
    auto id = entry->Attribute( "id" );
    if ( id )
    {
      Ability& abil = abilities[id];
      abil.name = id;
      if ( _stricmp( entry->Name(), "CAbilTrain" ) == 0 )
        abil.type = AbilType_Train;
      else if ( _stricmp( entry->Name(), "CAbilWarpTrain" ) == 0 )
      {
        abil.type = AbilType_Train;
        abil.warp = true;
      }
      else if ( _stricmp( entry->Name(), "CAbilMorph" ) == 0 )
        abil.type = AbilType_Morph;
      else if ( _stricmp( entry->Name(), "CAbilMorphPlacement" ) == 0 )
        abil.type = AbilType_MorphPlacement;
      else if ( _stricmp( entry->Name(), "CAbilBuild" ) == 0 )
        abil.type = AbilType_Build;
      else if ( _stricmp( entry->Name(), "CAbilMerge" ) == 0 )
        abil.type = AbilType_Merge;
      else if ( _stricmp( entry->Name(), "CAbilResearch" ) == 0 )
        abil.type = AbilType_Research;
      else if ( _stricmp( entry->Name(), "CAbilEffectTarget" ) == 0 )
        abil.type = AbilType_EffectTarget;
      else if ( _stricmp( entry->Name(), "CAbilEffectInstant" ) == 0 )
        abil.type = AbilType_EffectInstant;
      else if ( _stricmp( entry->Name(), "CAbilBehavior" ) == 0 )
        abil.type = AbilType_Behavior;

      printf_s( "[+] ability: %s\r\n", abil.name.c_str() );

      /* Todo support for AbilEffect*:
        <Charge Link = "Abil/HunterSeekerMissile" / >
        <Cooldown Link = "Abil/HunterSeekerMissile" / >
      */

      auto field = entry->FirstChildElement();
      while ( field )
      {
        if ( _strcmpi( field->Name(), "MorphUnit" ) == 0 && field->Attribute( "value" ) )
          abil.morphUnit = field->Attribute( "value" );
        else if ( _strcmpi( field->Name(), "Effect" ) == 0 && field->Attribute( "value" ) )
          abil.effect = field->Attribute( "value" );
        else if ( _strcmpi( field->Name(), "FlagArray" ) == 0 ) // CAbilBuild has these
        {
          if ( field->Attribute( "index" ) && field->Attribute( "value" ) )
          {
            if ( _stricmp( field->Attribute( "index" ), "PeonKillFinish" ) == 0 )
              abil.buildFinishKillsPeon = ( field->IntAttribute( "value" ) > 0 );
            else if ( _stricmp( field->Attribute( "index" ), "Interruptible" ) == 0 )
              abil.buildInterruptible = ( field->IntAttribute( "value" ) > 0 );
          }
        }
        else if ( _strcmpi( field->Name(), "Flags" ) == 0 ) // CAbilTrain has these
        {
          if ( field->Attribute( "index" ) && field->Attribute( "value" ) )
          {
            if ( _stricmp( field->Attribute( "index" ), "KillOnFinish" ) == 0 )
              abil.trainFinishKills = ( field->IntAttribute( "value" ) > 0 );
            else if ( _stricmp( field->Attribute( "index" ), "KillOnCancel" ) == 0 )
              abil.trainCancelKills = ( field->IntAttribute( "value" ) > 0 );
          }
        }
        else if ( _strcmpi( field->Name(), "Cost" ) == 0 ) // CAbilEffectTarget, CAbilEffectInstant has these
        {
          auto sub = field->FirstChildElement();
          while ( sub )
          {
            if ( _strcmpi( sub->Name(), "Vital" ) == 0 && sub->Attribute( "index" ) && sub->Attribute( "value" ) )
            {
              abil.vitalCosts[string( sub->Attribute( "index" ) )] = sub->DoubleAttribute( "value" );
            }
            sub = sub->NextSiblingElement();
          }
        }
        else if ( _strcmpi( field->Name(), "Range" ) == 0 && field->Attribute( "value" ) ) // CAbilEffectTarget has these
        {
          abil.range = field->DoubleAttribute( "value" );
        }
        else if ( _strcmpi( field->Name(), "Arc" ) == 0 && field->Attribute( "value" ) ) // CAbilEffectTarget has these
        {
          abil.arc = field->DoubleAttribute( "value" );
        }
        else if ( _strcmpi( field->Name(), "InfoArray" ) == 0 )
        {
          auto idx = field->Attribute( "index" );
          if ( ( abil.type == AbilType_Train || abil.type == AbilType_Build ) && idx ) // for CAbilTrain & CAbilBuild
          {
            if ( abil.commands.find( idx ) == abil.commands.end() )
              abil.commands[idx] = AbilityCommand( idx );

            AbilityCommand& cmd = abil.commands[idx];
            if ( field->Attribute( "Time" ) )
              cmd.time = field->DoubleAttribute( "Time" );

            if ( field->Attribute( "Delay" ) )
              cmd.delay = field->DoubleAttribute( "Delay" );

            auto unit = field->Attribute( "Unit" );
            if ( unit )
              cmd.units.emplace_back( unit );

            if ( field->FirstChildElement( "Unit" ) )
              cmd.units.clear();
            auto sub = field->FirstChildElement();
            while ( sub )
            {
              if ( _strcmpi( sub->Name(), "Vital" ) == 0 && sub->Attribute( "index" ) && _strcmpi( sub->Attribute( "index" ), "Energy" ) == 0 && sub->Attribute( "value" ) )
                cmd.energyCost = sub->DoubleAttribute( "value" );
              if ( _strcmpi( sub->Name(), "Unit" ) == 0 && sub->Attribute( "value" ) )
                cmd.units.emplace_back( sub->Attribute( "value" ) );
              else if ( _strcmpi( sub->Name(), "Button" ) == 0 && sub->Attribute( "Requirements" ) )
                cmd.requirements = sub->Attribute( "Requirements" );
              sub = sub->NextSiblingElement();
            }
          }
          else if ( ( abil.type == AbilType_Morph || abil.type == AbilType_MorphPlacement ) && field->Attribute( "Unit" ) ) // for CAbilMorph
          {
            // Note: current implementation misses CAbilMorphs and others that descend from a "parent" (attribute in AbilData.xml)
            // this includes at least TerranBuildingLiftOff, DisguiseChangeling and such
            if ( abil.commands.find( "Execute" ) == abil.commands.end() )
              abil.commands["Execute"] = AbilityCommand( "Execute" );

            AbilityCommand& cmd = abil.commands["Execute"];
            cmd.units.emplace_back( field->Attribute( "Unit" ) );

            auto sect = field->FirstChildElement( "SectionArray" );
            while ( sect )
            {
              if ( sect->Attribute( "index" ) && _stricmp( sect->Attribute( "index" ), "Actor" ) == 0 )
              {
                auto dur = sect->FirstChildElement( "DurationArray" );
                if ( dur && dur->Attribute( "index" ) && _stricmp( dur->Attribute( "index" ), "Delay" ) == 0 && dur->Attribute( "value" ) )
                  cmd.time = dur->DoubleAttribute( "value" );
              }
              sect = sect->NextSiblingElement( "SectionArray" );
            }
          }
          else if ( abil.type == AbilType_Research && idx ) // for CAbilResearch
          {
            if ( field->Attribute( "Upgrade" ) && strlen( field->Attribute( "Upgrade" ) ) < 1 ) // Empty "Upgrade" field means resetting
            {
              auto it = abil.commands.find( idx );
              if ( it != abil.commands.end() )
                abil.commands.erase( it );
            }
            else // Must have index attribute
            {
              if ( idx && abil.commands.find( idx ) == abil.commands.end() )
                abil.commands[idx] = AbilityCommand( idx );

              AbilityCommand& cmd = abil.commands[idx];
              if ( field->Attribute( "Time" ) )
                cmd.time = field->DoubleAttribute( "Time" );

              if ( field->Attribute( "Upgrade" ) )
              {
                cmd.isUpgrade = true;
                cmd.upgrade = field->Attribute( "Upgrade" );
              }
              auto sub = field->FirstChildElement();
              while ( sub )
              {
                if ( _strcmpi( sub->Name(), "Resource" ) == 0 && sub->Attribute( "index" ) && _stricmp( sub->Attribute( "index" ), "Minerals" ) == 0 && sub->Attribute( "value" ) )
                  cmd.mineralCost = sub->Int64Attribute( "value" );
                else if ( _strcmpi( sub->Name(), "Resource" ) == 0 && sub->Attribute( "index" ) && _stricmp( sub->Attribute( "index" ), "Vespene" ) == 0 && sub->Attribute( "value" ) )
                  cmd.vespeneCost = sub->Int64Attribute( "value" );
                else if ( _strcmpi( sub->Name(), "Button" ) == 0 && sub->Attribute( "Requirements" ) )
                  cmd.requirements = sub->Attribute( "Requirements" );
                sub = sub->NextSiblingElement();
              }
            }
          }
        }
        else if ( ( abil.type == AbilType_Morph || abil.type == AbilType_MorphPlacement || abil.type == AbilType_EffectTarget || abil.type == AbilType_EffectInstant ) &&  _strcmpi( field->Name(), "CmdButtonArray" ) == 0 && field->Attribute( "index" ) && _stricmp( field->Attribute( "index" ), "Execute" ) == 0 )
        {
          if ( abil.commands.find( "Execute" ) == abil.commands.end() )
            abil.commands["Execute"] = AbilityCommand( "Execute" );
          if ( field->Attribute( "Requirements" ) )
            abil.commands["Execute"].requirements = field->Attribute( "Requirements" );
        }
        else if ( ( abil.type == AbilType_Behavior) && _strcmpi( field->Name(), "CmdButtonArray" ) == 0 && field->Attribute( "index" ) )
        {
          if ( _stricmp( field->Attribute( "index" ), "On" ) == 0 || abil.commands.find( "On" ) == abil.commands.end() )
            abil.commands["On"] = AbilityCommand( "On" );
          if ( _stricmp( field->Attribute( "index" ), "Off" ) == 0 || abil.commands.find( "Off" ) == abil.commands.end() )
            abil.commands["Off"] = AbilityCommand( "Off" );
          if ( field->Attribute( "Requirements" ) )
            abil.commands[field->Attribute( "index" )].requirements = field->Attribute( "Requirements" );
        }
        else if ( abil.type == AbilType_Merge && _strcmpi( field->Name(), "Info" ) == 0 )
        {
          // archon merge, kinda hardcoded hack

          if ( abil.commands.find( "SelectedUnits" ) == abil.commands.end() )
            abil.commands["SelectedUnits"] = AbilityCommand( "SelectedUnits" );

          AbilityCommand& cmd = abil.commands["SelectedUnits"];

          auto sub = field->FirstChildElement();
          while ( sub )
          {
            if ( _strcmpi( sub->Name(), "Resource" ) == 0 && sub->Attribute( "index" ) && _stricmp( sub->Attribute( "index" ), "Minerals" ) == 0 && sub->Attribute( "value" ) )
              cmd.mineralCost = sub->Int64Attribute( "value" );
            else if ( _strcmpi( sub->Name(), "Resource" ) == 0 && sub->Attribute( "index" ) && _stricmp( sub->Attribute( "index" ), "Vespene" ) == 0 && sub->Attribute( "value" ) )
              cmd.vespeneCost = sub->Int64Attribute( "value" );
            else if ( _strcmpi( sub->Name(), "Button" ) == 0 && sub->Attribute( "Requirements" ) )
              cmd.requirements = sub->Attribute( "Requirements" );
            sub = sub->NextSiblingElement();
          }

          if ( field->Attribute( "Unit" ) )
          {
            cmd.units.emplace_back( field->Attribute( "Unit" ) );
            abil.morphUnit = field->Attribute( "Unit" );
          }
          if ( field->Attribute( "Time" ) )
            cmd.time = field->DoubleAttribute( "Time" );
        }
        field = field->NextSiblingElement();
      }
    }
    entry = entry->NextSiblingElement();
  }
}

const char* raceStr( Race race )
{
  if ( race == Race_Terran )
    return "terran";
  else if ( race == Race_Zerg )
    return "zerg";
  else if ( race == Race_Protoss )
    return "protoss";
  else
    return "neutral";
}

const char* resourceStr( ResourceType type )
{
  if ( type == Resource_Minerals )
    return "minerals";
  else if ( type == Resource_Vespene )
    return "vespene";
  else if ( type == Resource_Terrazine )
    return "terrazine";
  else if ( type == Resource_Custom )
    return "custom";
  else
    return "";
}

const char* abilTypeStr( AbilType type )
{
  if ( type == AbilType_Build )
    return "build";
  else if ( type == AbilType_Morph || type == AbilType_MorphPlacement )
    return "morph";
  else if ( type == AbilType_Train )
    return "train";
  else if ( type == AbilType_Research )
    return "research";
  else if ( type == AbilType_EffectTarget )
    return "effect_target";
  else if ( type == AbilType_EffectInstant )
    return "effect_instant";
  else if ( type == AbilType_Behavior )
    return "behavior";
  else
    return "";
}

void jsonPolyvecWrite( PolygonVector& polyvec, Json::Value& arr )
{
  for ( auto& poly : polyvec )
  {
    Json::Value jpoly( Json::arrayValue );
    for ( auto& pt : poly )
    {
      Json::Value jpt( Json::arrayValue );
      jpt.append( pt.x );
      jpt.append( pt.y );
      jpoly.append( jpt );
    }
    arr.append( jpoly );
  }
}

void resolveFootprint( const string& name, FootprintMap& footprints, Json::Value& out )
{
  if ( name.empty() || footprints.find( name ) == footprints.end() )
    return;

  auto& fp = footprints[name];

  out["name"] = fp.id;

  Json::Value shapes( Json::objectValue );
  shapes["radius"] = fp.shape.radius;
  Json::Value polygons( Json::arrayValue );
  jsonPolyvecWrite( fp.shape.unpathablePolys, polygons );
  shapes["unpathable"] = polygons;
  polygons.clear();
  jsonPolyvecWrite( fp.shape.buildingPolys, polygons );
  shapes["building"] = polygons;
  out["shape"] = shapes;

  Json::Value offset( Json::arrayValue );
  offset.append( fp.x );
  offset.append( fp.y );
  out["offset"] = offset;

  Json::Value dim( Json::arrayValue );
  dim.append( fp.w );
  dim.append( fp.h );
  out["dimensions"] = dim;

  string data;
  for ( int y = 0; y < fp.h; y++ )
    for ( int x = 0; x < fp.w; x++ )
    {
      auto idx = y * fp.w + x;
      data.append( ( fp.placement[idx] ? "x" : fp.creep[idx] ? "o" : fp.nearResources[idx] ? "n" : "." ) );
    }
  out["data"] = data;
}

void dumpUnits( UnitMap& units, FootprintMap& footprints )
{
  printf_s( "[d] dumping units...\r\n" );

  ofstream out;
  out.open( "units.json" );

  Json::Value root;
  for ( auto& unit : units )
  {
    if ( unit.second.lifeStart == 0 && unit.second.lifeMax == 0 )
      continue;

    Json::Value uval( Json::objectValue );
    uval["name"] = unit.second.name;

    uval["race"] = raceStr( unit.second.race );
    uval["food"] = unit.second.food;

    uval["mineralCost"] = static_cast<Json::UInt64>( unit.second.mineralCost );
    uval["vespeneCost"] = static_cast<Json::UInt64>( unit.second.vespeneCost );

    uval["speed"] = unit.second.speed;
    uval["acceleration"] = unit.second.acceleration;
    uval["speedMultiplierCreep"] = unit.second.speedMultiplierCreep;

    uval["radius"] = unit.second.radius;
    uval["sight"] = unit.second.sight;

    uval["lifeStart"] = unit.second.lifeStart;
    uval["lifeMax"] = unit.second.lifeMax;
    uval["lifeRegenRate"] = unit.second.lifeRegenRate;
    uval["lifeArmor"] = static_cast<Json::UInt64>( unit.second.lifeArmor );
    uval["shieldsStart"] = unit.second.shieldsStart;
    uval["shieldsMax"] = unit.second.shieldsMax;

    uval["energyStart"] = unit.second.energyStart;
    uval["energyMax"] = unit.second.energyMax;
    uval["energyRegenRate"] = unit.second.energyRegenRate;

    uval["light"] = unit.second.light;
    uval["biological"] = unit.second.biological;
    uval["mechanical"] = unit.second.mechanical;
    uval["armored"] = unit.second.armored;
    uval["structure"] = unit.second.structure;
    uval["psionic"] = unit.second.psionic;
    uval["massive"] = unit.second.massive;
    uval["cargoSize"] = static_cast<Json::UInt64>( unit.second.cargoSize );
    uval["turningRate"] = unit.second.turningRate;

    uval["shieldRegenDelay"] = unit.second.shieldRegenDelay;
    uval["shieldRegenRate"] = unit.second.shieldRegenRate;
    uval["mover"] = unit.second.mover;

    uval["scoreMake"] = static_cast<Json::UInt64>( unit.second.scoreMake );
    uval["scoreKill"] = static_cast<Json::UInt64>( unit.second.scoreKill );

    Json::Value collval( Json::arrayValue );
    for ( auto& c : unit.second.collides )
      collval.append( c );
    uval["collides"] = collval;

    uval["attackTargetPriority"] = unit.second.attackTargetPriority;
    uval["stationaryTurningRate"] = unit.second.stationaryTurningRate;
    uval["lateralAcceleration"] = unit.second.lateralAcceleration;

    Json::Value techals( Json::arrayValue );
    if ( !unit.second.techAliases.empty() )
      for ( auto& a : unit.second.techAliases )
        if ( !a.empty() )
          techals.append( a );

    uval["techAlias"] = techals;

    uval["aiEvalFactor"] = unit.second.aiEvalFactor;

    string evalAs = unit.second.aiEvaluateAlias;
    boost::replace_all( evalAs, "##id##", unit.second.name );
    if ( !evalAs.empty() )
    {
      if ( g_unitMapping.find( evalAs ) != g_unitMapping.end() )
      {
        auto evalid = g_unitMapping[evalAs];
        if ( evalid != 0 && evalid != g_unitMapping[unit.second.name] )
          uval["aiEvaluateAs"] = static_cast<Json::UInt64>( evalid );
      }
      if ( !boost::iequals( unit.second.name, evalAs ) )
        uval["aiEvaluateAsName"] = evalAs;
    }

    string glosAl = unit.second.glossaryAlias;
    boost::replace_all( glosAl, "##id##", unit.second.name );
    if ( !glosAl.empty() )
    {
      if ( g_unitMapping.find( glosAl ) != g_unitMapping.end() )
      {
        auto glosid = g_unitMapping[glosAl];
        if ( glosid != 0 && glosid != g_unitMapping[unit.second.name] )
          uval["glossaryAlias"] = static_cast<Json::UInt64>( glosid );
      }
      if ( !boost::iequals( unit.second.name, glosAl ) )
        uval["glossaryAliasName"] = glosAl;
    }

    Json::Value planeval( Json::arrayValue );
    for ( auto& c : unit.second.planeArray )
      planeval.append( c );
    uval["planes"] = planeval;

    Json::Value fp( Json::objectValue );
    resolveFootprint( unit.second.footprint, footprints, fp );
    uval["footprint"] = fp;

    Json::Value resval( Json::arrayValue );
    if ( unit.second.resourceType != Resource_None )
    {
      resval.append( resourceStr( unit.second.resourceType ) );
      resval.append( unit.second.resourceHarvestable ? "harvestable" : "raw" );
    }
    uval["resource"] = resval;
    uval["invulnerable"] = unit.second.invulnerable;

    Json::Value weapons( Json::arrayValue );
    for ( auto& weapon : unit.second.weapons )
      weapons.append( weapon );
    uval["weapons"] = weapons;

    Json::Value abils( Json::arrayValue );
    //for ( auto& abil : unit.second.abilityCommands )
    //  abils.append( abil );
    for ( auto& card : unit.second.abilityCardsMap )
      if ( !card.second.removed )
        for ( auto& abil : card.second.commands )
          if ( !abil.second.empty() )
            abils.append( abil.second );

    uval["abilityCommands"] = abils;

    Json::Value behvs( Json::arrayValue );
    for ( auto& bhv : unit.second.behaviors )
      if ( !bhv.second.empty() )
        behvs.append( bhv.second );
    uval["behaviors"] = behvs;

    root[std::to_string( g_unitMapping[unit.second.name] )] = uval;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( root, &out );

  out.close();
}

bool resolveRequirements( const string& useNodeName, Json::Value& rqtmp, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  if ( nodes.find( useNodeName ) == nodes.end() || useNodeName.size() < 5 ) // quick hack to identify numerals
  {
    rqtmp["type"] = "value";
    rqtmp["value"] = atoi( useNodeName.c_str() );
    return true;
  }
  auto& node = nodes[useNodeName];
  if ( node.type != ReqNode_Unknown )
  {
    if ( node.type == ReqNode_LogicAnd || node.type == ReqNode_LogicOr || node.type == ReqNode_LogicEq || node.type == ReqNode_LogicNot )
    {
      if ( node.operands.empty() )
        return false;
      rqtmp["type"] = ( node.type == ReqNode_LogicAnd ? "and" : node.type == ReqNode_LogicOr ? "or" : node.type == ReqNode_LogicEq ? "eq" : "not" );
      Json::Value rqops( Json::arrayValue );
      for ( auto& op : node.operands )
      {
        Json::Value optmp( Json::objectValue );
        if ( !resolveRequirements( op.second, optmp, requirements, nodes ) )
          continue;
        rqops.append( optmp );
      }
      rqtmp["operands"] = rqops;
    }
    else
    {
      rqtmp["type"] = ( node.type == ReqNode_CountUnit ? "unitCount" : node.type == ReqNode_CountUpgrade ? "upgradeCount" : "" );
      if ( node.type == ReqNode_CountUnit )
      {
        Json::Value namesVal( Json::arrayValue );
        Json::Value idsVal( Json::arrayValue );
        for ( auto& name : resolveAlias( node.countLink ) )
        {
          namesVal.append( name );
          idsVal.append( static_cast<Json::UInt64>( g_unitMapping[name] ) );
        }
        rqtmp["unitName"] = namesVal;
        rqtmp["unit"] = idsVal;
      }
      else if ( node.type == ReqNode_CountUpgrade )
      {
        rqtmp["upgradeName"] = node.countLink;
        rqtmp["upgrade"] = static_cast<Json::UInt64>( g_upgradeMapping[node.countLink] );
      }
      if ( !node.countState.empty() )
        rqtmp["state"] = node.countState;
    }
    return true;
  }
  else
    return false;
}

size_t resolveAbilityCmd( const string& ability, const string& command )
{
  size_t cmdindex = 0;
  if ( boost::iequals( command, "Execute" ) ) // morphs
    cmdindex = 0;
  else if ( command.size() > 5 && boost::iequals( command.substr( 0, 5 ), "Build" ) ) // builds
  {
    string numpart = command.substr( 5 );
    cmdindex = ( atoi( numpart.c_str() ) - 1 );
  }
  else if ( command.size() > 5 && boost::iequals( command.substr( 0, 5 ), "Train" ) ) // trains
  {
    string numpart = command.substr( 5 );
    cmdindex = ( atoi( numpart.c_str() ) - 1 );
  }
  else if ( command.size() > 8 && boost::iequals( command.substr( 0, 8 ), "Research" ) ) // researches
  {
    string numpart = command.substr( 8 );
    cmdindex = ( atoi( numpart.c_str() ) - 1 );
  }
  else if ( boost::iequals( command, "On" ) ) // CAbilBehavior on/off
    cmdindex = 0;
  else if ( boost::iequals( command, "Off" ) ) // CAbilBehavior on/off
    cmdindex = 1;
  string idx = ( ability + "," );
  idx.append( std::to_string( cmdindex ) );

  return g_abilityMapping[idx];
}

void filtersToJSON( std::set<FilterAttribute>& attribs, Json::Value& out )
{
  for ( const auto& tk : attribs )
  {
    if ( tk == Search_Ground )
      out.append( "ground" );
    if ( tk == Search_Structure )
      out.append( "structure" );
    if ( tk == Search_Self )
      out.append( "self" );
    if ( tk == Search_Player )
      out.append( "player" );
    if ( tk == Search_Ally )
      out.append( "ally" );
    if ( tk == Search_Air )
      out.append( "air" );
    if ( tk == Search_Stasis )
      out.append( "stasis" );
  }
};

bool resolveEffect( const string& owner, string name, Json::Value& out, EffectMap& effects )
{
  boost::replace_all( name, "##id##", owner );
  if ( effects.find( name ) != effects.end() )
  {
    Json::Value eval( Json::objectValue );
    auto& fx = effects[name];
    eval["name"] = fx.name;
    if ( fx.type == Effect::Effect_Missile )
    {
      eval["type"] = "missile";
      Json::Value subentry;
      if ( resolveEffect( name, fx.impactEffect, subentry, effects ) )
        eval["impact"] = subentry;
    }
    else if ( fx.type == Effect::Effect_Damage )
      eval["type"] = "damage";
    else if ( fx.type == Effect::Effect_CreateUnit )
      eval["type"] = "createUnit";
    else if ( fx.type == Effect::Effect_CreateHealer )
      eval["type"] = "createHealer";
    else if ( fx.type == Effect::Effect_Other )
      eval["type"] = "unknown";
    else if ( fx.type == Effect::Effect_Set )
    {
      eval["type"] = "set";
      Json::Value subfx( Json::arrayValue );
      for ( auto& p : fx.setSubEffects )
        if ( !p.second.empty() )
        {
          Json::Value subentry;
          if ( resolveEffect( name, p.second, subentry, effects ) )
            subfx.append( subentry );
        }
      eval["setEffects"] = subfx;
    }
    else if ( fx.type == Effect::Effect_Persistent )
    {
      eval["type"] = "persistent";
      Json::Value subfx( Json::arrayValue );
      for ( auto& p : fx.persistentEffects )
        if ( !p.empty() )
        {
          Json::Value subentry;
          if ( resolveEffect( name, p, subentry, effects ) )
            subfx.append( subentry );
        }
      Json::Value periods( Json::arrayValue );
      for ( auto p : fx.persistentPeriods )
        periods.append( p );
      eval["setEffects"] = subfx;
      if ( fx.periodCount > 0 )
        eval["persistentCount"] = static_cast<Json::UInt64>( fx.periodCount );
      if ( !fx.persistentPeriods.empty() )
        eval["persistentPeriods"] = periods;
    }

    if ( !fx.searchRequires.empty() )
    {
      Json::Value reqs( Json::arrayValue );
      filtersToJSON( fx.searchRequires, reqs );
      eval["searchRequires"] = reqs;
    }
    if ( !fx.searchExcludes.empty() )
    {
      Json::Value excls( Json::arrayValue );
      filtersToJSON( fx.searchExcludes, excls );
      eval["searchExcludes"] = excls;
    }

    if ( fx.type == Effect::Effect_Damage )
    {
      if ( fx.flagKill && fx.impactLocation == Effect::Impact_SourceUnit )
      {
        eval["type"] = "suicide";
      }
      else
      {
        eval["dmgAmount"] = fx.damageAmount;
        eval["dmgArmorReduction"] = fx.damageArmorReduction;
        eval["dmgKind"] = fx.damageKind;
        Json::Value attribBonuses( Json::objectValue );
        for ( auto& attribPair : fx.attributeBonuses )
          if ( attribPair.second.value != 0.0 )
            attribBonuses[attribPair.first] = attribPair.second.value;
        eval["dmgAttributeBonuses"] = attribBonuses;
        Json::Value splash( Json::arrayValue );
        for ( auto& area : fx.splashArea )
        {
          Json::Value entry( Json::objectValue );
          entry["fraction"] = area.second.fraction;
          entry["radius"] = area.second.radius;
          splash.append( entry );
        }
        eval["dmgSplash"] = splash;
      }
    }

    out = eval;
    return true;
  }
  return false;
}

void dumpWeapons( WeaponMap& weapons, EffectMap& effects )
{
  printf_s( "[d] dumping weapons...\r\n" );

  ofstream out;
  out.open( "weapons.json" );

  Json::Value root;
  for ( auto& wpn : weapons )
  {
    Json::Value wval( Json::objectValue );
    wval["name"] = wpn.second.name;
    wval["range"] = wpn.second.range;
    wval["period"] = wpn.second.period;
    wval["arc"] = wpn.second.arc;
    wval["damagePoint"] = wpn.second.damagePoint;
    wval["backSwing"] = wpn.second.backSwing;
    wval["rangeSlop"] = wpn.second.rangeSlop;
    wval["arcSlop"] = wpn.second.arcSlop;
    wval["minScanRange"] = wpn.second.minScanRange;
    wval["randomDelayMin"] = wpn.second.randomDelayMin;
    wval["randomDelayMax"] = wpn.second.randomDelayMax;
    wval["melee"] = wpn.second.melee;
    wval["hidden"] = wpn.second.hidden;
    wval["disabled"] = wpn.second.disabled;

    Json::Value reqs( Json::arrayValue );
    filtersToJSON( wpn.second.targetRequire, reqs );
    wval["filterRequires"] = reqs;

    Json::Value excs( Json::arrayValue );
    filtersToJSON( wpn.second.targetExclude, excs );
    wval["filterExcludes"] = excs;

    Json::Value effect( Json::objectValue );
    resolveEffect( wpn.second.name, wpn.second.effect, effect, effects );
    wval["effect"] = effect;

    root[wpn.second.name] = wval;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( root, &out );

  out.close();
}

void dumpAbilities( AbilityMap& abils, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  printf_s( "[d] dumping abilities...\r\n" );

  ofstream out;
  out.open( "abilities.json" );

  Json::Value root;
  for ( auto& abil : abils )
  {
    if ( abil.second.type == AbilType_Other )
      continue;

    Json::Value uval( Json::objectValue );
    uval["name"] = abil.second.name;
    uval["type"] = abilTypeStr( abil.second.type );
    if ( !abil.second.morphUnit.empty() )
      uval["morphUnit"] = abil.second.morphUnit;

    if ( abil.second.type == AbilType_EffectTarget )
    {
      uval["range"] = abil.second.range;
      uval["arc"] = abil.second.arc;
    }

    if ( !abil.second.vitalCosts.empty() )
    {
      Json::Value costtmp( Json::objectValue );
      for ( auto& cost : abil.second.vitalCosts )
      {
        string costname = cost.first;
        std::transform( costname.begin(), costname.end(), costname.begin(), ::tolower );
        costtmp[costname] = cost.second;
      }
      uval["costs"] = costtmp;
    }

    Json::Value cmds( Json::objectValue );
    for ( auto& cmd : abil.second.commands )
    {
      if ( abil.second.type == AbilType_Train && cmd.second.units.empty() )
        continue;

      Json::Value cval;
      cval["index"] = static_cast<Json::UInt64>( resolveAbilityCmd( abil.second.name, cmd.second.index ) );
      cval["time"] = cmd.second.time;
      if ( cmd.second.energyCost > 0.0 )
        cval["energy"] = cmd.second.energyCost;
      if ( cmd.second.mineralCost > 0.0 )
        cval["minerals"] = cmd.second.mineralCost;
      if ( cmd.second.vespeneCost > 0.0 )
        cval["vespene"] = cmd.second.vespeneCost;
      if ( cmd.second.delay > 0.0 )
        cval["delay"] = cmd.second.delay;
      if ( !cmd.second.requirements.empty() )
      {
        Json::Value reqsnode( Json::arrayValue );
        vector<string> rqs;
        if ( requirements.find( cmd.second.requirements ) != requirements.end() )
        {
          if ( !requirements[cmd.second.requirements].useNodeName.empty() )
            rqs.push_back( requirements[cmd.second.requirements].useNodeName );
          if ( !requirements[cmd.second.requirements].showNodeName.empty() )
            rqs.push_back( requirements[cmd.second.requirements].showNodeName );
        }
        else
          rqs.push_back( cmd.second.requirements );
        for ( auto& r : rqs )
        {
          Json::Value optmp( Json::objectValue );
          if ( resolveRequirements( r, optmp, requirements, nodes ) )
            reqsnode.append( optmp );
        }
        cval["requires"] = reqsnode;
      }

      if ( !cmd.second.units.empty() )
      {
        Json::Value units( Json::arrayValue );
        for ( auto& unit : cmd.second.units )
          if ( !unit.empty() )
            units.append( unit );

        cval["units"] = units;
      }

      cmds[cmd.second.index] = cval;
    }

    uval["commands"] = cmds;

    root[abil.second.name] = uval;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( root, &out );

  out.close();
}

struct TechTreeBuildEntry {
  string unit;
  int unitCount = 0;
  string ability;
  string command;
  double time = 0.0;
  string requirements;
  bool buildInterruptible = false;
  bool finishKillsPeon = false;
  bool trainFinishKills = false;
  bool trainCancelKills = false;
};

struct TechTreeResearchEntry {
  string upgrade;
  string ability;
  string command;
  double time = 0.0;
  string requirements;
  int64_t minerals = 0;
  int64_t vespene = 0;
};

struct TechTreeEntry {
  string id;
  vector<TechTreeBuildEntry> builds {};
  vector<TechTreeBuildEntry> morphs {};
  vector<TechTreeBuildEntry> merges {};
  vector<TechTreeResearchEntry> researches {};
};

using TechTree = vector<TechTreeEntry>;

using TechMap = std::map<Race, TechTree>;

void generateTechTree( UnitMap& units, AbilityMap& abilities, Race race, TechTree& tree )
{
  UnitMap buildings;
  for ( auto& unit : units )
  {
    if ( unit.second.race != race )
      continue;

    // some cleanup
    if ( boost::iequals( unit.second.name.substr( 0, 7 ), "XelNaga" ) && !boost::iequals( unit.second.name, "XelNagaTower" ) )
      continue;
    if ( boost::iequals( unit.second.name.substr( 0, 4 ), "Aiur" )
      || boost::iequals( unit.second.name.substr( 0, 8 ), "PortCity" )
      || boost::iequals( unit.second.name.substr( 0, 8 ), "Shakuras" )
      || boost::iequals( unit.second.name.substr( 0, 19 ), "SnowRefinery_Terran" )
      || boost::iequals( unit.second.name.substr( 0, 15 ), "ExtendingBridge" ) )
      continue;

    // hardcode to get rid of mothership core; i think blizzard screwed up in their cmdcard XML regarding this. it might even be possible to still build one in a melee game.
    if ( boost::iequals( unit.second.name, "MothershipCore" ) )
      continue;

    TechTreeEntry entry;
    entry.id = unit.second.name;

    // for ( auto& name : unit.second.abilityCommands )
    for ( auto& card : unit.second.abilityCardsMap )
      if ( !card.second.removed )
        for ( auto& cmdpair : card.second.commands )
        {
          if ( cmdpair.second.empty() )
            continue;

          string name = cmdpair.second;
          // "Ability,Command"
          vector<string> parts;
          boost::split( parts, name, boost::is_any_of( "," ) );
          if ( parts.size() != 2 )
            continue;

          auto& ability = abilities[parts[0]];
          if ( ability.type == AbilType_Train || ability.type == AbilType_Build || ability.type == AbilType_Morph || ability.type == AbilType_MorphPlacement || ability.type == AbilType_Merge )
          {
            auto& cmd = ability.commands[parts[1]];

            if ( cmd.units.empty() || cmd.isUpgrade )
              continue;

            TechTreeBuildEntry bentry;
            // training zergling has ["zergling", "zergling"] for example, so this simplification seems reasonable
            // but skip cocoons and eggs, we don't care
            for ( auto& u : cmd.units )
            {
              if ( u.find( "Cocoon" ) != std::string::npos )
                continue;
              if ( u.find( "Egg" ) != std::string::npos )
                continue;
              bentry.unit = u;
              break;
            }

            // cannot morph to myself. this might happen eg ObserverSiegeMode descends from Observer but doesn't override the morph skill.
            if ( ( ability.type == AbilType_Morph || ability.type == AbilType_MorphPlacement ) && boost::iequals( bentry.unit, unit.second.name ) )
              continue;

            // hardcode to get rid of mothership core
            if ( boost::iequals( bentry.unit, "MothershipCore" ) )
              continue;

            if ( bentry.unit.empty() )
            {
              bentry.unit = cmd.units[cmd.units.size() - 1]; // else just choose the last one which is usually better than first
            }
            bentry.unitCount = ( bentry.unit.empty() ? 0 : 1 );
            // hardcode hack to fix zerglings, literally the only unit that's built 2 at a time
            if ( ability.type == AbilType_Train && boost::iequals( bentry.unit, "Zergling" ) )
            {
              bentry.unitCount = 2;
            }
            bentry.ability = ability.name;
            bentry.time = cmd.time;
            bentry.command = cmd.index;
            bentry.requirements = cmd.requirements;

            if ( ability.type == AbilType_Build )
            {
              bentry.buildInterruptible = ability.buildInterruptible;
              bentry.finishKillsPeon = ability.buildFinishKillsPeon;
            }
            else if ( ability.type == AbilType_Train )
            {
              bentry.trainFinishKills = ability.trainFinishKills;
              bentry.trainCancelKills = ability.trainCancelKills;
            }

            // separate morphs from other types as morph consumes existing unit
            if ( ability.type == AbilType_Morph || ability.type == AbilType_MorphPlacement )
              entry.morphs.push_back( bentry );
            else if ( ability.type == AbilType_Merge )
              entry.merges.push_back( bentry );
            else
              entry.builds.push_back( bentry );
          }
          else if ( ability.type == AbilType_Research )
          {
            auto& cmd = ability.commands[parts[1]];
            if ( !cmd.isUpgrade )
              continue;

            TechTreeResearchEntry res;
            res.upgrade = cmd.upgrade;
            res.ability = ability.name;
            res.time = cmd.time;
            res.command = cmd.index;
            res.requirements = cmd.requirements;
            res.minerals = cmd.mineralCost;
            res.vespene = cmd.vespeneCost;
            entry.researches.push_back( res );
          }
        }

    tree.push_back( entry );

    buildings[unit.second.name] = unit.second;
  }
}

void dumpRequirementsJSON( const string& reqstr, RequirementMap& requirements, RequirementNodeMap& nodes, Json::Value& out )
{
  if ( !reqstr.empty() )
  {
    Json::Value reqsnode( Json::arrayValue );
    vector<string> rqs;
    if ( requirements.find( reqstr ) != requirements.end() )
    {
      if ( !requirements[reqstr].useNodeName.empty() )
        rqs.push_back( requirements[reqstr].useNodeName );
      if ( !requirements[reqstr].showNodeName.empty() )
        rqs.push_back( requirements[reqstr].showNodeName );
    }
    else
      rqs.push_back( reqstr );
    for ( auto& r : rqs )
    {
      Json::Value optmp( Json::objectValue );
      if ( resolveRequirements( r, optmp, requirements, nodes ) )
        reqsnode.append( optmp );
    }
    out["requires"] = reqsnode;
  }
}

void dumpTechTree( TechMap& techtree, RequirementMap& requirements, RequirementNodeMap& nodes )
{
  printf_s( "[d] dumping tech tree...\r\n" );

  ofstream out;
  out.open( "techtree.json" );

  Json::Value root;
  for ( auto& entry : techtree )
  {
    Json::Value racenode( Json::objectValue );
    for ( auto& asd : entry.second )
    {
      if ( asd.builds.empty() && asd.morphs.empty() && asd.researches.empty() )
        continue;

      Json::Value unitnode( Json::objectValue );
      unitnode["name"] = asd.id;

      if ( !asd.builds.empty() )
      {
        Json::Value buildsnode( Json::arrayValue );
        for ( auto& build : asd.builds )
        {
          auto abilityCmdIndex = resolveAbilityCmd( build.ability, build.command );

          Json::Value buildnode( Json::objectValue );
          buildnode["unit"] = static_cast<Json::UInt64>( g_unitMapping[build.unit] );
          buildnode["unitName"] = build.unit;

          if ( build.unitCount > 1 )
            buildnode["unitCount"] = build.unitCount;

          buildnode["abilityName"] = ( build.ability + "," + build.command );
          buildnode["ability"] = static_cast<Json::UInt64>( abilityCmdIndex );
          buildnode["time"] = build.time;

          if ( build.buildInterruptible ) // terran scv -> building; CAbilBuild
            buildnode["interruptible"] = true;
          if ( build.finishKillsPeon ) // terran scv -> building; CAbilBuild
            buildnode["finishKillsWorker"] = true;
          if ( build.trainFinishKills ) // zerg larva -> *; CAbilTrain
            buildnode["finishKillsSource"] = true;
          if ( build.trainCancelKills ) // zerg larva -> *; CAbilTrain
            buildnode["cancelKillsSource"] = true;

          dumpRequirementsJSON( build.requirements, requirements, nodes, buildnode );

          buildsnode.append( buildnode );
        }
        unitnode["builds"] = buildsnode;
      }

      if ( !asd.morphs.empty() )
      {
        Json::Value morphsnode( Json::arrayValue );
        for ( auto& morph : asd.morphs )
        {
          auto abilityCmdIndex = resolveAbilityCmd( morph.ability, morph.command );

          Json::Value morphnode( Json::objectValue );
          morphnode["unit"] = static_cast<Json::UInt64>( g_unitMapping[morph.unit] );
          morphnode["unitName"] = morph.unit;

          if ( morph.unitCount > 1 )
            morphnode["unitCount"] = morph.unitCount;

          morphnode["abilityName"] = ( morph.ability + "," + morph.command );
          morphnode["ability"] = static_cast<Json::UInt64>( abilityCmdIndex );
          morphnode["time"] = morph.time;

          dumpRequirementsJSON( morph.requirements, requirements, nodes, morphnode );

          morphsnode.append( morphnode );
        }
        unitnode["morphs"] = morphsnode;
      }

      if ( !asd.merges.empty() )
      {
        Json::Value mergesnode( Json::arrayValue );
        for ( auto& merge : asd.merges )
        {
          auto abilityCmdIndex = resolveAbilityCmd( merge.ability, merge.command );

          Json::Value mergenode( Json::objectValue );
          mergenode["unit"] = static_cast<Json::UInt64>( g_unitMapping[merge.unit] );
          mergenode["unitName"] = merge.unit;

          if ( merge.unitCount > 1 )
            mergenode["unitCount"] = merge.unitCount;

          mergenode["abilityName"] = ( merge.ability + "," + merge.command );
          mergenode["ability"] = static_cast<Json::UInt64>( abilityCmdIndex );
          mergenode["time"] = merge.time;

          dumpRequirementsJSON( merge.requirements, requirements, nodes, mergenode );

          mergesnode.append( mergenode );
        }
        unitnode["merges"] = mergesnode;
      }

      if ( !asd.researches.empty() )
      {
        Json::Value resnode( Json::arrayValue );
        for ( auto& res : asd.researches )
        {
          if ( res.upgrade.size() < 2 )
            continue;

          auto abilityCmdIndex = resolveAbilityCmd( res.ability, res.command );

          Json::Value resentry( Json::objectValue );
          resentry["upgrade"] = static_cast<Json::UInt64>( g_upgradeMapping[res.upgrade] );
          resentry["upgradeName"] = res.upgrade;
          resentry["abilityName"] = ( res.ability + "," + res.command );
          resentry["ability"] = static_cast<Json::UInt64>( abilityCmdIndex );
          resentry["time"] = res.time;
          resentry["minerals"] = static_cast<Json::UInt64>( res.minerals );
          resentry["vespene"] = static_cast<Json::UInt64>( res.vespene );

          dumpRequirementsJSON( res.requirements, requirements, nodes, resentry );

          resnode.append( resentry );
        }
        unitnode["researches"] = resnode;
      }

      auto numindex = std::to_string( g_unitMapping[asd.id] );

      racenode[numindex] = unitnode;
    }

    root[raceStr( entry.first )] = racenode;
  }

  Json::StreamWriterBuilder builder;
  builder["commentStyle"] = "None";
  builder["indentation"] = "  ";
  std::unique_ptr<Json::StreamWriter> writer( builder.newStreamWriter() );
  writer->write( root, &out );

  out.close();
}

void readGameData( const string& path, UnitMap& units, Unit& defaultUnit, Footprint& defaultFootprint, AbilityMap& abilities, RequirementMap& requirements, RequirementNodeMap& nodes, FootprintMap& footprints, WeaponMap& weapons, Weapon& defaultWeapon, EffectMap& effects )
{
  string unitDataPath = path + PATHSEP "UnitData.xml";

  size_t loops = 0;
  while ( true )
  {
    loops++;
    size_t notfound = 0;
    parseUnitData( unitDataPath, units, defaultUnit, notfound );
    if ( notfound == 0 )
      break;
    if ( loops > 10 )
      throw runtime_error( "Failed to parse unit data in 10 rounds, wtf?" );
  }

  string abilityDataPath = path + PATHSEP "AbilData.xml";
  parseAbilityData( abilityDataPath, abilities );

  string requirementDataPath = path + PATHSEP "RequirementData.xml";
  string requirementNodeDataPath = path + PATHSEP "RequirementNodeData.xml";
  parseRequirementData( requirementDataPath, requirementNodeDataPath, requirements, nodes );

  string footprintDataPath = path + PATHSEP "FootprintData.xml";
  loops = 0;
  while ( true )
  {
    loops++;
    size_t notfound = 0;
    parseFootprintData( footprintDataPath, footprints, defaultFootprint, notfound );
    if ( notfound == 0 )
      break;
    if ( loops > 10 )
      throw runtime_error( "Failed to parse footprint data in 10 rounds, wtf?" );
  }

  string weaponDataPath = path + PATHSEP "WeaponData.xml";
  parseWeaponData( weaponDataPath, weapons, defaultWeapon );

  string effectDataPath = path + PATHSEP "EffectData.xml";
  loops = 0;
  while ( true )
  {
    loops++;
    size_t notfound = 0;
    parseEffectData( effectDataPath, effects, notfound );
    if ( notfound == 0 )
      break;
    if ( loops > 10 )
      throw runtime_error( "Failed to parse effect data in 10 rounds, wtf?" );
  }
}

void readStableID( const string& path, NameToIDMapping& unitMapping, NameToIDMapping& abilityMapping, NameToIDMapping& upgradeMapping )
{
  Json::Value root;
  std::ifstream infile;
  infile.open( path, std::ifstream::in );
  if ( !infile.is_open() )
    throw runtime_error( "could not open stableid.json" );
  infile >> root;
  infile.close();

  auto& units = root["Units"];
  for ( auto& unit : units )
  {
    unitMapping[unit["name"].asString()] = unit["id"].asUInt64();
  }

  auto& abilities = root["Abilities"];
  for ( auto& ability : abilities )
  {
    string name = ( ability["name"].asString() + "," );
    name.append( ability["index"].asString() );
    abilityMapping[name] = ability["id"].asUInt64();
  }

  auto& upgrades = root["Upgrades"];
  for ( auto& upgrade : upgrades )
  {
    upgradeMapping[upgrade["name"].asString()] = upgrade["id"].asUInt64();
  }
}

void dumpTechTreeText( const string& suffix, TechTree& tree )
{
  // dump techtree to txt for debug
  ofstream techDump;
  techDump.open( "techtree-" + suffix + ".txt" );
  for ( auto& root : tree )
  {
    techDump << root.id << std::endl;
    if ( !root.builds.empty() )
      techDump << "  builds" << std::endl;
    for ( auto& bld : root.builds )
      if ( !bld.unit.empty() )
        techDump << "    " << bld.unit << std::endl;
    if ( !root.morphs.empty() )
      techDump << "  morphs" << std::endl;
    for ( auto& mrph : root.morphs )
      if ( !mrph.unit.empty() )
        techDump << "    " << mrph.unit << std::endl;
    if ( !root.merges.empty() )
      techDump << "  merges" << std::endl;
    for ( auto& mrg : root.merges )
      if ( !mrg.unit.empty() )
        techDump << "    " << mrg.unit << std::endl;
    if ( !root.researches.empty() )
    {
      techDump << "  upgrades" << std::endl;
      for ( auto& res : root.researches )
        if ( !res.upgrade.empty() )
          techDump << "    " << res.upgrade << std::endl;
    }
    techDump << std::endl;
  }
  techDump.close();
}

void cleanupUnitCommandCards( UnitMap& units )
{
  for ( auto& unit : units )
  {
    std::set<std::string> usedCommands;
    for ( auto& card : unit.second.abilityCardsMap )
    {
      if ( card.second.removed || card.second.commands.empty() )
        continue;
      auto it = card.second.commands.begin();
      while ( it != card.second.commands.end() )
      {
        if ( usedCommands.find( ( *it ).second ) != usedCommands.end() )
          it = card.second.commands.erase( it );
        else
        {
          usedCommands.insert( ( *it ).second );
          it++;
        }
      }
    }
  }
}

int main()
{
  string rootPath;
  rootPath.reserve( MAX_PATH );

#if defined( WIN32 )
  GetCurrentDirectoryA( MAX_PATH, &rootPath[0] );
#else
  getcwd( &rootPath[0], MAX_PATH );
#endif

  string stableIDPath = rootPath.c_str(); // clone from c_str because internally rootPath is corrupted
  stableIDPath.append( PATHSEP "stableid.json" );

  readStableID( stableIDPath, g_unitMapping, g_abilityMapping, g_upgradeMapping );

  // sc2 uses incremental patches so these have to be in order
  vector<string> mods = {
    "core.sc2mod",
    "liberty.sc2mod",
    "swarm.sc2mod",
    "void.sc2mod",
    "voidmulti.sc2mod",
    //"balancemulti.sc2mod"
  };

  UnitMap units;
  AbilityMap abilities;
  RequirementMap requirements;
  RequirementNodeMap nodes;
  FootprintMap footprints;
  WeaponMap weapons;
  Weapon defaultWeapon;
  Footprint defaultFootprint;
  EffectMap effects;
  Unit defaultUnit;

  for ( const auto& mod : mods )
  {
    string modPath = rootPath.c_str(); // clone from c_str because internally rootPath is corrupted
    modPath.append( PATHSEP "mods" PATHSEP + mod );

    printf_s( "[A] mod: %s\r\n", mod.c_str() );

    string gameDataPath = modPath + PATHSEP "base.sc2data" PATHSEP "GameData";
    readGameData( gameDataPath, units, defaultUnit, defaultFootprint, abilities, requirements, nodes, footprints, weapons, defaultWeapon, effects );
  }

  cleanupUnitCommandCards( units );

  dumpUnits( units, footprints );

  dumpAbilities( abilities, requirements, nodes );

  dumpWeapons( weapons, effects );

  TechTree zergTechTree;
  TechTree protossTechTree;
  TechTree terranTechTree;
  generateTechTree( units, abilities, Race_Zerg, zergTechTree );
  generateTechTree( units, abilities, Race_Protoss, protossTechTree );
  generateTechTree( units, abilities, Race_Terran, terranTechTree );

  TechMap techMap;
  techMap[Race_Zerg] = zergTechTree;
  techMap[Race_Protoss] = protossTechTree;
  techMap[Race_Terran] = terranTechTree;

  dumpTechTree( techMap, requirements, nodes );

  printf_s( "[d] dumping text files for humans...\r\n" );

  // dump footprints to text file with easy visualisation
  ofstream footDump;
  footDump.open( "footprints.txt" );
  for ( auto& fp : footprints )
  {
    if ( fp.second.removed || fp.second.placement.empty() || fp.second.w == -1 )
      continue;

    char sdfsd[128];
    sprintf_s( sdfsd, 128, " (%i,%i,%i,%i)", fp.second.x, fp.second.y, fp.second.w, fp.second.h );
    footDump << fp.second.id << sdfsd << std::endl;
    for ( int y = 0; y < fp.second.h; y++ )
    {
      for ( int x = 0; x < fp.second.w; x++ )
      {
        auto idx = y * fp.second.w + x;
        footDump << ( fp.second.placement[idx] ? "x" : fp.second.creep[idx] ? "o" : fp.second.nearResources[idx] ? "n" : "." );
      }
      footDump << std::endl;
    }
    footDump << std::endl;
  }
  footDump.close();

  dumpTechTreeText( "zerg", techMap[Race_Zerg] );
  dumpTechTreeText( "protoss", techMap[Race_Protoss] );
  dumpTechTreeText( "terran", techMap[Race_Terran] );

#if defined( WIN32 )
  system( "pause" );
#endif

  return EXIT_SUCCESS;
}
