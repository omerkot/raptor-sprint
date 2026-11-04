#include "regions.hpp"

namespace rr {

static const Region kTable[kRegions] = {
    {"Fern Hollow", "Misty woods. Learn to leap, duck and swerve.",
     {"Logs: jump (A)   Low branches: duck (B)", "Boulders: steer round them", "Brambles: jump them or run round"},
     {{0.44f, 0.50f, 0.45f}, 0.036f, {1.45f, 1.18f, 0.84f}, {-0.45f, 0.42f, -0.79f}, {0.30f, 0.37f, 0.35f}, {0.15f, 0.14f, 0.10f},
      {255, 255, 255, 255}, {255, 255, 255, 255}, {255, 255, 255, 255}}},
    {"Riverlands", "Bright mornings by the water.",
     {"Creeks: jump just before the bank", "Twin logs: land, and jump again at once", "Driftwood jams: too tall, steer for the gap"},
     {{0.52f, 0.60f, 0.62f}, 0.030f, {1.50f, 1.36f, 1.10f}, {0.30f, 0.55f, -0.78f}, {0.36f, 0.44f, 0.48f}, {0.16f, 0.16f, 0.13f},
      {236, 248, 236, 255}, {225, 255, 225, 255}, {245, 245, 245, 255}}},
    {"The Thicket", "Dense, dark undergrowth. Low wood everywhere.",
     {"Log, then branch: jump, then duck as you land", "Branch tunnels: keep ducking (hold B)", "Root gates: jump and hold duck together"},
     {{0.32f, 0.34f, 0.26f}, 0.045f, {1.30f, 1.00f, 0.65f}, {-0.60f, 0.35f, -0.70f}, {0.24f, 0.27f, 0.22f}, {0.13f, 0.11f, 0.08f},
      {225, 215, 195, 255}, {205, 215, 160, 255}, {230, 215, 200, 255}}},
    {"Granite Rise", "Cold stone slopes.",
     {"Boulder slaloms: weave left and right", "Boulder, then log: swerve, then jump", "Rolling boulders: sprint past or ease off"},
     {{0.50f, 0.53f, 0.57f}, 0.032f, {1.30f, 1.26f, 1.20f}, {0.50f, 0.50f, -0.70f}, {0.33f, 0.36f, 0.40f}, {0.15f, 0.15f, 0.15f},
      {205, 205, 200, 255}, {190, 215, 195, 255}, {215, 215, 220, 255}}},
    {"The Mire", "Swamp fog and sucking mud.",
     {"Mud: sprint (R) and leap from the very edge", "Creek, then branch: leap, then duck", "Sinkholes: they pull you in, steer away"},
     {{0.42f, 0.44f, 0.30f}, 0.050f, {1.20f, 1.10f, 0.60f}, {0.00f, 0.30f, -0.95f}, {0.28f, 0.30f, 0.20f}, {0.14f, 0.13f, 0.08f},
      {205, 190, 150, 255}, {215, 220, 140, 255}, {205, 200, 180, 255}}},
    {"Deadwood", "Dead trees at dusk. They fall.",
     {"Falling trees: they drop as you come near", "Gauntlets: everything, close together", "Ribcages: duck down the middle, leap the bone"},
     {{0.42f, 0.30f, 0.26f}, 0.040f, {1.60f, 0.85f, 0.45f}, {0.20f, 0.18f, -0.96f}, {0.28f, 0.22f, 0.24f}, {0.16f, 0.10f, 0.08f},
      {215, 180, 160, 255}, {215, 165, 120, 255}, {185, 172, 165, 255}}},
};

const Region& region(int i) { return kTable[i < 0 ? 0 : i >= kRegions ? kRegions - 1 : i]; }
int level_seed(int r, int k) { return 100 + r * 10 + k; }
float level_length(int r, int k) { return 1000.0f + 200.0f * k + 100.0f * r; }

}  // namespace rr
