/*
 * scope_guard.hpp
 *
 * Copyright (c) 2018-2021, SciresM.
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * Scope guard logic lovingly taken from Andrei Alexandrescu's "Systemic Error Handling in C++".
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

#ifndef __SCOPE_GUARD_HPP__
#define __SCOPE_GUARD_HPP__

#include "defines.h"

#define SCOPE_GUARD     ::nxdt::utils::ScopeGuardOnExit() + [&]() ALWAYS_INLINE_LAMBDA
#define ON_SCOPE_EXIT   auto ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE_) = SCOPE_GUARD

namespace nxdt::utils {
    template<class F>
    class ScopeGuard {
        NON_COPYABLE(ScopeGuard);
        
        private:
            F f;
            bool active;
        
        public:
            constexpr ALWAYS_INLINE ScopeGuard(F f) : f(std::move(f)), active(true) {}
            
            constexpr ALWAYS_INLINE ~ScopeGuard()
            {
                if (active) f();
            }
            
            constexpr ALWAYS_INLINE void Cancel()
            {
                active = false;
            }
            
            constexpr ALWAYS_INLINE ScopeGuard(ScopeGuard&& rhs) : f(std::move(rhs.f)), active(rhs.active)
            {
                rhs.Cancel();
            }
            
            ScopeGuard &operator=(ScopeGuard&& rhs) = delete;
    };
    
    template<class F>
    constexpr ALWAYS_INLINE ScopeGuard<F> MakeScopeGuard(F f)
    {
        return ScopeGuard<F>(std::move(f));
    }
    
    enum class ScopeGuardOnExit {};
    
    template <typename F>
    constexpr ALWAYS_INLINE ScopeGuard<F> operator+(ScopeGuardOnExit, F&& f)
    {
        return ScopeGuard<F>(std::forward<F>(f));
    }
}

#endif  /* __SCOPE_GUARD_HPP__ */
