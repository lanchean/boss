/*
    IdleBossHunter
    Copyright (C) 2020 Michael de Lang

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
#include <rapidjson/document.h>
#include "messages/message.h"

using namespace std;

namespace ibh {
    struct bonus {
        uint64_t stat_id;
        uint64_t amount;

        bonus(uint64_t stat_id, uint64_t amount) noexcept : stat_id(stat_id), amount(amount) {}
    };
    struct company {
        string name;
        vector<string> members;
        vector<bonus> bonuses;

        company(string name, vector<string> members, vector<bonus> bonuses) noexcept : name(move(name)), members(move(members)), bonuses(move(bonuses)) {}
    };

    struct get_company_listing_response : message {
        explicit get_company_listing_response(string error, vector<company> companies) noexcept;

        ~get_company_listing_response() noexcept override = default;

        [[nodiscard]]
        string serialize() const override;

        [[nodiscard]]
        static unique_ptr<get_company_listing_response> deserialize(rapidjson::Document const &d);

        string error;
        vector<company> companies;

        static constexpr uint64_t type = generate_type<get_company_listing_response>();
    };
}