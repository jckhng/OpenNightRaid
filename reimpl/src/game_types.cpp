#include "niteraid/game_types.hpp"

namespace niteraid {

std::string to_string(Screen screen)
{
    switch (screen) {
    case Screen::Title:
        return "Title";
    case Screen::Credits:
        return "Credits";
    case Screen::AttractInterlude:
        return "AttractInterlude";
    case Screen::ControlPanel:
        return "ControlPanel";
    case Screen::Gameplay:
        return "Gameplay";
    case Screen::Intermission:
        return "Intermission";
    case Screen::GameOver:
        return "GameOver";
    case Screen::HighScoreEntry:
        return "HighScoreEntry";
    case Screen::HighScores:
        return "HighScores";
    case Screen::Finale:
        return "Finale";
    case Screen::SharewareEnding:
        return "SharewareEnding";
    }

    return "UnknownScreen";
}

std::string to_string(GameplayState state)
{
    switch (state) {
    case GameplayState::Active:
        return "Active";
    case GameplayState::ScriptedSequence:
        return "ScriptedSequence";
    case GameplayState::Transition:
        return "Transition";
    case GameplayState::GameOver:
        return "GameOver";
    case GameplayState::LevelComplete:
        return "LevelComplete";
    case GameplayState::FinaleComplete:
        return "FinaleComplete";
    }

    return "UnknownGameplayState";
}

std::string to_string(ObjectType type)
{
    switch (type) {
    case ObjectType::None:
        return "None";
    case ObjectType::PlayerCannon:
        return "PlayerCannon";
    case ObjectType::WaveController:
        return "WaveController";
    case ObjectType::PlayerProjectile:
        return "PlayerProjectile";
    case ObjectType::Aircraft:
        return "Aircraft";
    case ObjectType::EnemyDeath:
        return "EnemyDeath";
    case ObjectType::AircraftDebris:
        return "AircraftDebris";
    case ObjectType::SmartBomb:
        return "SmartBomb";
    case ObjectType::Paratrooper:
        return "Paratrooper";
    case ObjectType::GroundedTransition:
        return "GroundedTransition";
    case ObjectType::LandedInvader:
        return "LandedInvader";
    case ObjectType::ResolutionParticle:
        return "ResolutionParticle";
    case ObjectType::FinaleController:
        return "FinaleController";
    case ObjectType::Presenter:
        return "Presenter";
    }

    return "UnknownObjectType";
}

std::string to_string(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D:
        return "VariantD";
    case AircraftVariant::A:
        return "VariantA";
    case AircraftVariant::B:
        return "VariantB";
    case AircraftVariant::C:
        return "VariantC";
    }

    return "UnknownVariant";
}

std::string_view aircraft_bucket(AircraftVariant variant)
{
    switch (variant) {
    case AircraftVariant::D:
    case AircraftVariant::B:
        return "BigPlane";
    case AircraftVariant::A:
    case AircraftVariant::C:
        return "LittlePlane";
    }

    return "UnknownBucket";
}

}  // namespace niteraid
