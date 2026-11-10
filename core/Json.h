#pragma once
#include "../third_party/json/json.hpp"
#include "Pickle.h"
namespace OffHanger
{
using Json = nlohmann::ordered_json;
ValuePtr FromJson(Json const& Json);
Json ToJson(ValuePtr const& Value);
ValuePtr ReadJson(Bytes const& Bytes);
Bytes WriteJson(ValuePtr const& Value);
} // namespace OffHanger
