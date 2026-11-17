#pragma once
// First ground-parity tier. Never exceed the captured server run-speed limit.
namespace groundtuning {
constexpr float MaxYards=14.f;
// Server-acknowledged transport envelope, NOT a cap/boost in Skate physics.
constexpr float TransportYards=70.f;
static bool NaturalMomentum=false;
inline float Maximum(){return NaturalMomentum?TransportYards:MaxYards;}
constexpr float PushMultiplier=1.8f;
constexpr float AccelerationMultiplier=1.5f;
constexpr int SteeringAxis=32767;
}
