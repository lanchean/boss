/*
    IdleBossHunter
    Copyright (C) 2019 Michael de Lang

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <string>
#include <optional>
#include <rapidjson/document.h>
#include "messages/message.h"
#include "messages/objects/account_object.h"

using namespace std;

namespace ibh {
    struct user_entered_game_response : message {
        explicit user_entered_game_response(account_object user) noexcept;

        ~user_entered_game_response() noexcept override = default;

        [[nodiscard]]
        string serialize() const override;

        [[nodiscard]]
        static unique_ptr<user_entered_game_response> deserialize(rapidjson::Document const &d);

        account_object user;

        inline static constexpr uint64_t type = generate_type<user_entered_game_response>();
    };
}
