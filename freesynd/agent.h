/************************************************************************
 *                                                                      *
 *  FreeSynd - a remake of the classic Bullfrog game "Syndicate".       *
 *                                                                      *
 *   Copyright (C) 2005  Stuart Binge  <skbinge@gmail.com>              *
 *   Copyright (C) 2005  Joost Peters  <joostp@users.sourceforge.net>   *
 *   Copyright (C) 2006  Trent Waddington <qg@biodome.org>              *
 *   Copyright (C) 2006  Tarjei Knapstad <tarjei.knapstad@gmail.com>    *
 *                                                                      *
 *    This program is free software;  you can redistribute it and / or  *
 *  modify it  under the  terms of the  GNU General  Public License as  *
 *  published by the Free Software Foundation; either version 2 of the  *
 *  License, or (at your option) any later version.                     *
 *                                                                      *
 *    This program is  distributed in the hope that it will be useful,  *
 *  but WITHOUT  ANY WARRANTY;  without even  the implied  warranty of  *
 *  MERCHANTABILITY  or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU  *
 *  General Public License for more details.                            *
 *                                                                      *
 *    You can view the GNU  General Public License, online, at the GNU  *
 *  project's  web  site;  see <http://www.gnu.org/licenses/gpl.html>.  *
 *  The full text of the license is also included in the file COPYING.  *
 *                                                                      *
 ************************************************************************/
#ifndef AGENT_H
#define AGENT_H

#include "common.h"
#include "weaponholder.h"
#include <string>
#include <vector>

class Mod;
class WeaponInstance;

#define SLOT_LEGS 5
#define SLOT_ARMS 4
#define SLOT_CHEST 3
#define SLOT_HEART 2
#define SLOT_EYES 1
#define SLOT_BRAIN 0

/*!
 * Stores information about agent health, weapons inventory and modifications.
 */
class Agent : public WeaponHolder {
public:
    Agent(const char *agent_name, bool male);
    ~Agent();

    const char *name() { return name_.c_str(); }
    bool isMale() { return male_; }
    bool isActive() { return active_; }
    void setActive(bool a) { active_ = a; }
    int health() { return health_; }

    void setHealth(int n) {
        if (n > 255)
            n = 255;

        health_ = n;
    }

    Mod *slot(int n) {
        assert(n < 6);
        return slots_[n];
    }

    void setSlot(int n, Mod *m) {
        assert(n < 6);
        slots_[n] = m;
    }

    void clearSlots() {
        for (int i = 0; i < 6; i++)
            slots_[i] = NULL;
    }

    void removeAllWeapons();

protected:
    std::string name_;
    bool male_;
    bool active_;
    int health_;
    Mod *slots_[6];
};

#endif
