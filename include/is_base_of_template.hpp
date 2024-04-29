/*
 * is_base_of_template.hpp
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * Based on goneskiing's C++ implementation at:
 * https://stackoverflow.com/a/63562826
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __IS_BASE_OF_TEMPLATE_HPP__
#define __IS_BASE_OF_TEMPLATE_HPP__

#include <variant>
#include <experimental/type_traits>

namespace nxdt::utils
{
    /* Can be used as part of static asserts to check if any given class was derived from a base template class. */
    template <template <typename...> class Base, typename Derived>
    struct is_base_of_template
    {
        template <typename... Ts> static constexpr std::variant<Ts...> is_callable(Base<Ts...>*);

        template <typename T> using is_callable_t = decltype(is_callable(std::declval<T*>()));

        static inline constexpr bool value = std::experimental::is_detected_v<is_callable_t, Derived>;

        using type = std::experimental::detected_or_t<void, is_callable_t, Derived>;
    };

    template <template <typename...> class Base, typename Derived>
    using is_base_of_template_t = typename is_base_of_template<Base, Derived>::type;

    template <template <typename...> class Base, typename Derived>
    inline constexpr bool is_base_of_template_v = is_base_of_template<Base, Derived>::value;
}

#endif  /* __IS_BASE_OF_TEMPLATE_HPP__ */
