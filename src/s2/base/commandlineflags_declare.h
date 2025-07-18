// Copyright Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#ifndef S2_BASE_COMMANDLINEFLAGS_DECLARE_H_
#define S2_BASE_COMMANDLINEFLAGS_DECLARE_H_

#include <cstdint>
#include <string>

#include "absl/flags/declare.h"


#define S2_DECLARE_bool(name) ABSL_DECLARE_FLAG(bool, name)

#define S2_DECLARE_double(name) ABSL_DECLARE_FLAG(double, name)

#define S2_DECLARE_int32(name) ABSL_DECLARE_FLAG(int32_t, name)

#define S2_DECLARE_int64(name) ABSL_DECLARE_FLAG(int64_t, name)

#define S2_DECLARE_string(name) ABSL_DECLARE_FLAG(std::string, name)

#endif  // S2_BASE_COMMANDLINEFLAGS_DECLARE_H_
