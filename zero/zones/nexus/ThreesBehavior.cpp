#include "ThreesBehavior.h"

#include <zero/behavior/BehaviorBuilder.h>
#include <zero/behavior/BehaviorTree.h>
#include <zero/behavior/nodes/AimNode.h>
#include <zero/behavior/nodes/AttachNode.h>
#include <zero/behavior/nodes/BlackboardNode.h>
#include <zero/behavior/nodes/ChatNode.h>
#include <zero/behavior/nodes/InputActionNode.h>
#include <zero/behavior/nodes/MapNode.h>
#include <zero/behavior/nodes/MathNode.h>
#include <zero/behavior/nodes/MoveNode.h>
#include <zero/behavior/nodes/PlayerNode.h>
#include <zero/behavior/nodes/PowerballNode.h>
#include <zero/behavior/nodes/RegionNode.h>
#include <zero/behavior/nodes/RenderNode.h>
#include <zero/behavior/nodes/ShipNode.h>
#include <zero/behavior/nodes/ThreatNode.h>
#include <zero/behavior/nodes/TimerNode.h>
#include <zero/behavior/nodes/WaypointNode.h>
#include <zero/zones/nexus/Nexus.h>
#include <zero/zones/nexus/nodes/LowestTargetNode.h>
#include <zero/zones/nexus/nodes/NearestTeammateNode.h>
#include <zero/zones/nexus/nodes/NearestTeammatePlayerPositionQueryNode.h>
#include <zero/zones/nexus/nodes/PlayerByNameNode.h>
#include <zero/zones/svs/nodes/BurstAreaQueryNode.h>
#include <zero/zones/svs/nodes/DynamicPlayerBoundingBoxQueryNode.h>
#include <zero/zones/svs/nodes/FindNearestGreenNode.h>
#include <zero/zones/svs/nodes/IncomingDamageQueryNode.h>
#include <zero/zones/svs/nodes/MemoryTargetNode.h>
#include <zero/zones/svs/nodes/NearbyEnemyWeaponQueryNode.h>
#include <zero/zones/trenchwars/nodes/AttachNode.h>

using namespace zero::svs;

namespace zero {
namespace nexus {

// TODO WIP
struct Placeholder : public behavior::BehaviorNode {
  Placeholder(const char* something) : something(something) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    float enter_delay = (ctx.bot->game->connection.settings.EnterDelay / 100.0f);
    Player* self = ctx.bot->game->player_manager.GetSelf();

    // Make sure we are in a ship and not dead.
    if (!self || self->ship == 8) return behavior::ExecuteResult::Success;
    if (self->enter_delay > 0.0f && self->enter_delay < enter_delay) return behavior::ExecuteResult::Success;

    auto opt_tw = ctx.blackboard.Value<Nexus*>("nex");
    float radius = ctx.bot->game->connection.settings.ShipSettings[self->ship].GetRadius();
    Nexus* nexus = *opt_tw;

    path::Path entrance_path = ctx.bot->bot_controller->pathfinder->FindPath(
        ctx.bot->game->GetMap(), self->position, nexus->entrance_position, radius, self->frequency);

    // if (entrance_path.GetRemainingDistance() < nearby_threshold) {
    //  return behavior::ExecuteResult::Success;
    // }
    return behavior::ExecuteResult::Success;
  }

  const char* something = nullptr;
};

struct ShotSpreadNode : public behavior::BehaviorNode {
  ShotSpreadNode(const char* aimshot_key, float spread, float period)
      : aimshot_key(aimshot_key), spread(spread), period(period) {}

  behavior::ExecuteResult Execute(behavior::ExecuteContext& ctx) override {
    Player* self = ctx.bot->game->player_manager.GetSelf();
    if (!self || self->ship >= 8) return behavior::ExecuteResult::Failure;

    auto opt_aimshot = ctx.blackboard.Value<Vector2f>(aimshot_key);
    if (!opt_aimshot) return behavior::ExecuteResult::Failure;
    Vector2f aimshot = *opt_aimshot;

    Vector2f aim_direction = Normalize(aimshot - self->position);
    Vector2f perp = Perpendicular(aim_direction);

    if (period <= 0.0f) {
      period = 1.0f;
    }

    float t = GetTime();
    aimshot += perp * sinf(t / period) * spread;

    ctx.blackboard.Set(aimshot_key, aimshot);

    return behavior::ExecuteResult::Success;
  }

  inline float GetTime() { return GetMicrosecondTick() / (kTickDurationMicro * 10.0f); }

  const char* aimshot_key = nullptr;
  float spread = 0.0f;
  float period = 1.0f;
};

std::unique_ptr<behavior::BehaviorNode> ThreesBehavior::CreateTree(behavior::ExecuteContext& ctx) {
  using namespace behavior;

  BehaviorBuilder builder;

  const Vector2f center(512, 512);

  // Used for target prio
  constexpr float kLowEnergyThreshold = 800.0f;         // Energy threshold to prio targets
  constexpr float kLowEnergyDistanceThreshold = 20.0f;  // Distance threshold for prio targets

  // Don't dodge below this
  constexpr float kLowEnergyRushThreshold = 400.0f;  // Rush threshold
  constexpr float kRushDistanceThreshold = 15.0f;    // We will rush if someone is low energy within this range
  constexpr u32 kRushRepelThreshold = 1;             // If we don't have this many reps dont rush targets

  // Check for incoming damage within this range
  constexpr float kRepelDistance = 7.0f;

  // How much damage that is going towards an enemy before we start bombing. This is to limit the frequency of our
  // bombing so it overlaps bullets and is harder to dodge.
  constexpr float kBombRequiredDamageOverlap = 300.0f;

  // How far away a target needs to be before we start varying our shots around the target.
  constexpr float kShotSpreadDistanceThreshold = 40.0f;

  //  If an enemy is near us and we're low energy thor if below this value
  constexpr float kThorEnemyThreshold = 200.0f;

  // How far away from a teammate before we regroup
  constexpr float kTeamRange = 40.0f;

  constexpr float kLeashDistance = 30.0f;

  constexpr float kAvoidTeamDistance = 6.0f;

  //.Child<ReadConfigIntNode<u16>>("queue_command1", "command1")
  //.Child<ReadConfigIntNode<u16>>("queue_command2", "command2")
  //.Child<ReadConfigIntNode<u16>>("queue_command3", "command3")
  //.Child<ChatMessageNode>(ChatMessageNode::PublicBlackboard("command1")) // Invert so this fails and freq is
  // reevaluated. .Child<ChatMessageNode>(ChatMessageNode::PublicBlackboard("command2")) // Invert so this fails and
  // freq is reevaluated. .Child<ChatMessageNode>(ChatMessageNode::PublicBlackboard("command3")) // Invert so this fails
  // and freq is reevaluated.
  // clang-format off
  builder
    .Selector()
        .Sequence() //Join the queue first thing and auto join TODO: add command or checks to requeue if something goes wrong later such as recycled arena
            //.InvertChild<BlackboardSetQueryNode>("queued") //Check if we have already joined the queue, if not join
            .Child<TimerExpiredNode>("queue")
            .Child<ChatMessageNode>(ChatMessageNode::Public("?next 3v3pub"))
            .Child<ChatMessageNode>(ChatMessageNode::Public("?return")) 
            .Child<TimerSetNode>("queue", 6000)
            //.Child<ScalarNode>(1.0f, "queued")  //was only joining queue on join then stopped working
            .End()
        .Sequence() // Don't do anything while in spec
            .Child<PlayerFrequencyQueryNode>("self_freq")
            .Child<EqualityNode<u16>>("self_freq", 8025)  //Check spec
            .Child<ScalarNode>(1.0f, "spectating")
            .End()
        .Sequence() // Match startup begins when we get taken out of spec (since we sit in spec when waiting) 
            .Child<BlackboardSetQueryNode>("spectating")  //We just came out of spectating 
            .Child<TimerSetNode>("match_startup", 600)  //Trigger match start timer (assumming 3 sec + however long it takes the other person to ready up)
            .Child<BlackboardEraseNode>("spectating")
            .End()
        .Sequence() // Enter the specified ship if not already in it and have been taken out of spec.
            .InvertChild<TimerExpiredNode>("match_startup")            
            .InvertChild<ShipQueryNode>("request_ship")
            .Child<ShipRequestNode>("request_ship")
            .End()
        .Sequence()  // Fire 1 shot startup shot and set targets position to monitor for when they move so we can get out of the ready check loop
            .InvertChild<TimerExpiredNode>("match_startup")      
            .Child<TimerExpiredNode>("pre_fire")  // just needs to be longer than match_start
            .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bullet)
            .Child<InputActionNode>(InputAction::Bullet)
            .Child<TimerSetNode>("pre_fire", 1500)
            .End()
        .Sequence() //Attach if someone is safe and we have full energy
            .Child<BlackboardSetQueryNode>("tchat_safe")
            .InvertChild<TimerExpiredNode>("tchat_safe_timer")
            .Child<PlayerEnergyPercentThresholdNode>(1.0f)
            .Child<TimerExpiredNode>("attach_cooldown")
            .InvertChild<AttachedQueryNode>("self")
            .Child<NearestTeammateNode>("nearest_teammate") 
            .Child<PlayerPositionQueryNode>("nearest_teammate", "nearest_teammate_position")
            .Child<DistanceThresholdNode>("nearest_teammate_position", kTeamRange) //If we're already near teammates dont run to them               
            .Child<PlayerByNameNode>("tchat_safe", "tchat_safe_player")
            .Child<AttachNode>("tchat_safe_player")
            .Child<TimerSetNode>("attach_cooldown", 100)
            .Child<BlackboardEraseNode>("tchat_safe")
            .Child<BlackboardEraseNode>("tchat_safe_timer")
            .End()
        .Sequence() // Detach if attached
            .Child<AttachedQueryNode>()
            .Child<TimerExpiredNode>("attach_cooldown")
            .Child<DetachNode>()
            .Child<TimerSetNode>("attach_cooldown", 100)       
            .End()
        .Selector() // Choose to fight the player or follow waypoints.
            .Sequence() // Find nearest target and either path to them or seek them directly.              
                .Sequence(CompositeDecorator::Success)
                    .Child<PlayerPositionQueryNode>("self_position")
                    .Sequence() 
                        .Child<NearestMemoryTargetNode>("target")
                        .Child<PlayerPositionQueryNode>("target", "target_position")
                        .Child<NearestTeammatePlayerPositionQueryNode>("target", "target_nearest_teammate_position")
                        .Child<PlayerEnergyQueryNode>("target", "target_energy")
                        .Child<AimNode>(WeaponType::Bullet, "target", "aimshot")
                        .Child<PlayerPositionQueryNode>("target", "nearest_target_position") //Addionally copy to nearest so we can use it later
                        .Child<AimNode>(WeaponType::Bullet, "target", "nearest_aimshot")
                        .End()
                     .Sequence() //If is someone low nearby override target 
                        .Child<TimerExpiredNode>("recharge_timer") //Nearest target should be used when recharing
                        .Child<LowestTargetNode>("lowest_target")
                        .Child<PlayerPositionQueryNode>("lowest_target", "lowest_target_position")
                        .Child<PlayerEnergyQueryNode>("lowest_target", "lowest_target_energy")
                        .InvertChild<DistanceThresholdNode>("lowest_target_position", "self_position", kLowEnergyDistanceThreshold)
                        .InvertChild<ScalarThresholdNode<float>>("lowest_target_energy", kLowEnergyThreshold)
                        .Child<LowestTargetNode>("target")
                        .Child<PlayerPositionQueryNode>("target", "target_position")  //Override
                        .Child<NearestTeammatePlayerPositionQueryNode>("target", "target_nearest_teammate_position") //Override
                        .Child<PlayerEnergyQueryNode>("target", "target_energy")  //Override
                        .Child<AimNode>(WeaponType::Bullet, "target", "aimshot") //Override
                        .End()
                .End()
                .Sequence(CompositeDecorator::Success) // If we have a portal but no location, lay one down.
                    .Child<ShipItemCountThresholdNode>(ShipItemType::Portal, 1)
                    .InvertChild<ShipPortalPositionQueryNode>()
                    .Child<InputActionNode>(InputAction::Portal)
                    .End()
                .Selector(CompositeDecorator::Success) // Enable multifire if ship supports it and it's disabled.
                    .Sequence()
                        .Child<ShipCapabilityQueryNode>(ShipCapability_Multifire)
                        .Child<DistanceThresholdNode>("target_position", 35.0f) // If we are far from enemy, use multifire
                        .InvertChild<ShipMultifireQueryNode>()  //Check if multifire is off
                        .InvertChild<BlackboardSetQueryNode>("rushing") //dont multi if rushing
                        .Child<InputActionNode>(InputAction::Multifire) //Turn on multifire
                        .End()
                    .Sequence()
                        .Child<ShipCapabilityQueryNode>(ShipCapability_Multifire)
                        .InvertChild<DistanceThresholdNode>("target_position", 35.0f) // If we are far from enemy, turn off multifire
                        .Child<ShipMultifireQueryNode>()  //Check if multifire is on
                        .Child<InputActionNode>(InputAction::Multifire)  //Turn off multifire
                        .End()
                    .End()
                .Selector(CompositeDecorator::Success) // Toggle antiwarp based on energy
                    .Sequence() // Enable antiwarp if we are healthy
                        .Child<TimerExpiredNode>("tchat_safe_timer")  
                        .Child<ShipCapabilityQueryNode>(ShipCapability_Antiwarp)
                        .Child<PlayerEnergyPercentThresholdNode>(0.75f)
                        .InvertChild<PlayerStatusQueryNode>(Status_Antiwarp)
                        .Child<InputActionNode>(InputAction::Antiwarp)
                        .End()
                    .Sequence() // Disable antiwarp if we aren't healthy
                        .Child<ShipCapabilityQueryNode>(ShipCapability_Antiwarp)
                        .InvertChild<ShipQueryNode>(7) //Don't turn off if we're in a lance
                        .InvertChild<PlayerEnergyPercentThresholdNode>(0.75f)
                        .Child<PlayerStatusQueryNode>(Status_Antiwarp)
                        .Child<InputActionNode>(InputAction::Antiwarp)
                        .End()
                    .End()
                .Selector()
                    .Sequence() // Attempt to dodge and use defensive items.
                        .Sequence(CompositeDecorator::Success) // Always check incoming damage so we can use it in repel and portal sequences.
                            .Child<IncomingDamageQueryNode>(kRepelDistance, "incoming_damage")
                            .Child<PlayerCurrentEnergyQueryNode>("self_energy")
                            .End()
                        .Sequence(CompositeDecorator::Success) // If we are in danger but can't repel, use our portal.
                            .InvertChild<ShipItemCountThresholdNode>(ShipItemType::Repel)
                            .Child<ShipPortalPositionQueryNode>() // Check if we have a portal down.
                            .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")  //If incoming damage is > than our current energy
                            .Child<TimerExpiredNode>("defense_timer")
                            .Child<InputActionNode>(InputAction::Warp)
                            .Child<TimerSetNode>("defense_timer", 100)
                            .End()
                        .Sequence(CompositeDecorator::Success) // Use repel when in danger.
                            .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Repel)
                            .Child<TimerExpiredNode>("defense_timer")
                            .Child<ScalarThresholdNode<float>>("incoming_damage", "self_energy")  //If incoming damage is > than our current energy
                            .Child<InputActionNode>(InputAction::Repel)
                            .Child<TimerSetNode>("defense_timer", 100)
                            .End()
                        .Sequence(CompositeDecorator::Invert) // Check if enemy is very low energy and close to use. Don't bother dodging if they are rushing us with low energy.
                            .InvertChild<ScalarThresholdNode<float>>("target_energy", kLowEnergyRushThreshold)
                            .InvertChild<DistanceThresholdNode>("target_position", "self_position", 6.0f)
                            .End()
                        .Child<DodgeIncomingDamage>(0.2f, 30.0f)
                        .End()
                    .Sequence()  //Keep enemy distance while reacharging
                        .InvertChild<BlackboardSetQueryNode>("rushing")
                        .Selector()
                            .InvertChild<TimerExpiredNode>("recharge_timer")
                            .InvertChild<TimerExpiredNode>("bomb_disengage")
                            .End()
                        .Child<SeekNode>("aimshot", kLeashDistance, SeekNode::DistanceResolveType::Dynamic)
                        .End()
                    .Sequence() // Path to teammate if far away
                        .Child<TimerExpiredNode>("match_startup") 
                        .InvertChild<BlackboardSetQueryNode>("rushing")
                        .Child<NearestTeammateNode>("nearest_teammate", 2) //Make sure we have at least 1 teammate close, if more than one stay with the broader group
                        .Child<PlayerPositionQueryNode>("nearest_teammate", "nearest_teammate_position")
                        .Child<DistanceThresholdNode>("nearest_teammate_position", kTeamRange) //If we're already near teammates dont run to them
                        .Child<ScalarThresholdNode<float>>("target_energy", kLowEnergyThreshold)  //If we're going for a kill or someone is diving dont run
                        .Child<GoToNode>("nearest_teammate_position")
                        .Child<RenderPathNode>(Vector3f(0.0f, 1.0f, 0.5f))
                        .End()
                    .Sequence() // Path to target if they aren't immediately visible.
                        .Child<TimerExpiredNode>("match_startup") 
                        .InvertChild<VisibilityQueryNode>("target_position")
                        .Child<GoToNode>("target_position")
                        .Child<AvoidTeamNode>(kAvoidTeamDistance)
                        .Child<RenderPathNode>(Vector3f(0.0f, 1.0f, 0.5f))
                        .End()
                    .Sequence() // Aim at target and shoot while seeking them.
                        .Child<TimerExpiredNode>("match_startup") 
                        .Sequence(CompositeDecorator::Success)
                            .Child<DistanceThresholdNode>("target_position", kShotSpreadDistanceThreshold)
                            .Child<ShotSpreadNode>("aimshot", 3.0f, 1.0f)
                            .End()
                        .Parallel()     
                            .Child<FaceNode>("aimshot")
                            .Child<BlackboardEraseNode>("rushing")                      
                            .Selector()
                               .Sequence() // If there is any low target with in this range prioritize
                                    .Child<ShipItemCountThresholdNode>(ShipItemType::Repel, kRushRepelThreshold) //dont go into rush mode with no reps
                                    .InvertChild<DistanceThresholdNode>("target_position", "self_position", kRushDistanceThreshold)
                                    .InvertChild<ScalarThresholdNode<float>>("target_energy", kLowEnergyRushThreshold)
                                    .Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Static)
                                    .Child<ScalarNode>(1.0f, "rushing")
                                    .Sequence(CompositeDecorator::Success) //Optionally rocket if the target is too far and we have decent energy
                                        .Child<ShipItemCountThresholdNode>(ShipItemType::Rocket)
                                        .Child<PlayerEnergyPercentThresholdNode>(0.6f)
                                        .InvertChild<DistanceThresholdNode>("target_position", 30.0f)  //dont rocket if too far away
                                        .Child<DistanceThresholdNode>("target_position", 10.0f)  //dont rocket if right on them you'll overshoot
                                        .Child<TimerExpiredNode>("rocket_timer")
                                        .Child<InputActionNode>(InputAction::Rocket)
                                        .Child<TimerSetNode>("rocket_timer", 1500)
                                        .End()
                                    .Child<BlackboardEraseNode>("recharge_timer")
                                    .End()
                                .Sequence() 
                                    .InvertChild<PlayerEnergyPercentThresholdNode>(0.35f)
                                    .Child<TimerSetNode>("recharge_timer", 850)  
                                    .Sequence(CompositeDecorator::Success)
                                        .InvertChild<ShipItemCountThresholdNode>(ShipItemType::Repel)
                                        .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Decoy)
                                        .Child<TimerExpiredNode>("decoy_timer")
                                        .Child<InputActionNode>(InputAction::Decoy)
                                        .Child<TimerSetNode>("decoy_timer", 850)    
                                        .End()
                                    .End()
                                .Sequence(CompositeDecorator::Success)
                                    .Child<TimerExpiredNode>("match_startup") 
                                    //.Child<SeekNode>("aimshot", 0.0f, SeekNode::DistanceResolveType::Zero)
                                    .Child<SeekNode>("aimshot", 15.0f, SeekNode::DistanceResolveType::Dynamic)
                                    .InvertChild<BlackboardSetQueryNode>("rushing")
                                    .Child<AvoidTeamNode>(kAvoidTeamDistance)
                                    .End()
                                .End()
                            .Sequence(CompositeDecorator::Success) // Bomb fire check.
                                .Child<TimerExpiredNode>("match_startup") 
                                .Child<TimerExpiredNode>("recharge_timer") 
                                .InvertChild<BlackboardSetQueryNode>("rushing")
                                .Child<VectorSubtractNode>("aimshot", "self_position", "target_direction", true)
                                .Child<PlayerVelocityQueryNode>("self_velocity")
                                .Child<VectorDotNode>("self_velocity", "target_direction", "forward_velocity")
                                .Child<ScalarThresholdNode<float>>("forward_velocity", 2.0f)
                                .Child<PlayerEnergyPercentThresholdNode>(0.45f)
                                .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Bomb)
                                .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bomb)
                                .InvertChild<InputQueryNode>(InputAction::Thor)
                                .Child<IncomingDamageQueryNode>("target", kRepelDistance * 2.5f, 2.75f, "outgoing_damage")
                                .Child<ScalarThresholdNode<float>>("outgoing_damage", kBombRequiredDamageOverlap) // Check if we have enough bullets overlapping outgoing damage to fire a bomb into.
                                .InvertChild<DistanceThresholdNode>("nearest_target_position", 50.0f)  //dont bomb from too far
                                .Child<DistanceThresholdNode>("target_position", "target_nearest_teammate_position", 12.0f)  //dont bomb at our target if we or a teammate is near them
                                .Child<ShotVelocityQueryNode>(WeaponType::Bomb, "bomb_fire_velocity")
                                .Child<RayNode>("self_position", "bomb_fire_velocity", "bomb_fire_ray")
                                .Child<DynamicPlayerBoundingBoxQueryNode>("target", "target_bounds", 4.0f)
                                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
                                .Child<RenderRayNode>("world_camera", "bomb_fire_ray", 50.0f, Vector3f(1.0f, 1.0f, 0.0f))
                                .Child<RayRectangleInterceptNode>("bomb_fire_ray", "target_bounds")
                                .Child<InputActionNode>(InputAction::Bomb)
                                .Child<TimerSetNode>("bomb_disengage", 150)
                                .End()
                            .Sequence(CompositeDecorator::Success) // PB thor fire check.
                                .Child<TimerExpiredNode>("match_startup")
                                .InvertChild<PlayerEnergyPercentThresholdNode>(0.25f)  //If we are low energy            
                                .Child<ShipWeaponCapabilityQueryNode>(WeaponType::Thor)  //If we can thor
                                .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Thor)  //If its not on cd
                                .InvertChild<InputQueryNode>(InputAction::Bomb)  //If we're not bombing
                                .InvertChild<ScalarThresholdNode<float>>("target_energy", kThorEnemyThreshold)  //If the enemy is low health
                                .InvertChild<DistanceThresholdNode>("target_position", 8.0f) //If the enemy within pb range
                                .Child<ShotVelocityQueryNode>(WeaponType::Thor, "thor_fire_velocity")
                                .Child<RayNode>("self_position", "thor_fire_velocity", "thor_fire_ray")
                                .Child<DynamicPlayerBoundingBoxQueryNode>("target", "target_bounds", 4.0f)
                                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
                                .Child<RenderRayNode>("world_camera", "thor_fire_ray", 50.0f, Vector3f(1.0f, 1.0f, 0.0f))
                                .Child<RayRectangleInterceptNode>("thor_fire_ray", "target_bounds")
                                .Child<InputActionNode>(InputAction::Thor) //Thor
                                .End()
                            .Sequence(CompositeDecorator::Success) // Determine if a shot should be fired by using weapon trajectory and bounding boxes.
                                .Child<TimerExpiredNode>("match_startup")             
                                .Child<TimerExpiredNode>("recharge_timer") 
                                .Child<DynamicPlayerBoundingBoxQueryNode>("target", "target_bounds", 4.0f)
                                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                .Child<RenderRectNode>("world_camera", "target_bounds", Vector3f(1.0f, 0.0f, 0.0f))
                                .Selector()
                                    .Child<BlackboardSetQueryNode>("rushing")
                                    .Child<PlayerEnergyPercentThresholdNode>(0.35f)
                                    .End()
                                .InvertChild<ShipWeaponCooldownQueryNode>(WeaponType::Bullet)
                                .InvertChild<InputQueryNode>(InputAction::Bomb) // Don't try to shoot a bullet when shooting a bomb.
                                .InvertChild<TileQueryNode>(kTileIdSafe)
                                .Child<ShotVelocityQueryNode>(WeaponType::Bullet, "bullet_fire_velocity")
                                .Child<RayNode>("self_position", "bullet_fire_velocity", "bullet_fire_ray")
                                .Child<DynamicPlayerBoundingBoxQueryNode>("target", "target_bounds", 4.0f)
                                .Child<MoveRectangleNode>("target_bounds", "aimshot", "target_bounds")
                                .Child<RayRectangleInterceptNode>("bullet_fire_ray", "target_bounds")
                                .Child<InputActionNode>(InputAction::Bullet)
                                .End()
                            .End()
                        .End()
                    .End()
                .End()
            .Sequence() // Follow set waypoints.
                .Child<TimerExpiredNode>("match_startup") 
                .Child<WaypointNode>("waypoints", "waypoint_index", "waypoint_position", 15.0f)
                .Selector()
                    .Sequence()
                        .InvertChild<VisibilityQueryNode>("waypoint_position")
                        .Child<GoToNode>("waypoint_position")
                        .Child<RenderPathNode>(Vector3f(0.0f, 0.5f, 1.0f))
                        .End()
                    .Parallel()
                        .Child<FaceNode>("waypoint_position")
                        .Child<ArriveNode>("waypoint_position", 1.25f)
                        .End()
                    .End()
                .End()
            .End()
        .End();
  // clang-format on

  return builder.Build();
}

}  // namespace nexus
}  // namespace zero