/*
League Overseer
    Copyright (C) 2013-2015 Vladimir Jimenez & Ned Anderson

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __MATCH_CAP_EVENT_H__
#define __MATCH_CAP_EVENT_H__

#include <string>

#include "MatchEvent.h"

class CaptureMatchEvent : public MatchEvent<CaptureMatchEvent>
{
    public:
        CaptureMatchEvent ();

        CaptureMatchEvent& setTeamID (int _teamID);
        CaptureMatchEvent& setTime   (std::string _time);
        CaptureMatchEvent& setBZID   (std::string _bzID);
        CaptureMatchEvent& save      (void);

    private:
        int         teamID;

        std::string matchTime,
                    bzID;
};

#endif