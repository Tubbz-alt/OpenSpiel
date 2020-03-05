// Copyright 2019 DeepMind Technologies Ltd. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "open_spiel/spiel.h"
#include "open_spiel/tests/basic_tests.h"

namespace open_spiel {
    namespace klondike {
        namespace {

            namespace testing = open_spiel::testing;

            void BasicKlondikeTests() {
                testing::LoadGameTest("klondike");
                testing::ChanceOutcomesTest(*LoadGame("klondike"));
                testing::RandomSimTest(*LoadGame("klondike"), 100);
                for (Player players = 1; players <= 1; players++) {
                    testing::RandomSimTest(
                            *LoadGame("klondike", {{"players", GameParameter(players)}}), 100);
                }
                testing::ResampleInfostateTest(*LoadGame("klondike"), /*num_sims=*/100);
            }

        }  // namespace
    }  // namespace klondike
}  // namespace open_spiel

int main(int argc, char** argv) { open_spiel::klondike::BasicKlondikeTests(); }
