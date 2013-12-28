/*
League Overseer
    Copyright (C) 2013-2014 Vladimir Jimenez & Ned Anderson

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

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include "bzfsAPI.h"
#include "plugin_utils.h"

// Define plugin version numbering
const int MAJOR = 1;
const int MINOR = 1;
const int REV = 0;
const int BUILD = 187;

// Log failed assertions at debug level 0 since this will work for non-member functions and it is important enough.
#define ASSERT(x)
{
    if (!(x)) // If the condition is not true, then send a debug message
    {
        bz_debugMessagef(0, "ERROR :: League Over Seer :: Failed assertion '%s' at %s:%d", #x, __FILE__, __LINE__);
    }
}

// A function that will get the player record by their callsign
static bz_BasePlayerRecord* bz_getPlayerByCallsign (const char* callsign)
{
    // Use a smart pointer so we don't have to worry about freeing up the memory
    // when we're done. In other words, laziness.
    std::unique_ptr<bz_APIIntList> playerList(bz_getPlayerIndexList());

    // Loop through all of the players' callsigns
    for (unsigned int i = 0; i < playerList->size(); i++)
    {
        // Have we found the callsign we're looking for?
        if (bz_getPlayerByIndex(playerList->get(i))->callsign == callsign)
        {
            // Return the record for that player
            return bz_getPlayerByIndex(playerList->get(i));
        }
    }

    // Return NULL if the callsign was not found
    return NULL;
}

// Convert a bz_eTeamType value into a string literal with the option
// of adding whitespace to format the string to return
static std::string formatTeam (bz_eTeamType teamColor, bool addWhiteSpace)
{
    // Because we may have to format the string with white space padding, we need to store
    // the value somewhere
    std::string color;

    // Switch through the supported team colors of this plugin
    switch (teamColor)
    {
        case eBlueTeam:
            color = "Blue";
            break;

        case eGreenTeam:
            color = "Green";
            break;

        case ePurpleTeam:
            color = "Purple";
            break;

        case eRedTeam:
            color = "Red";
            break;

        default:
            break;
    }

    // We may want to format the team color name with white space for the debug messages
    if (addWhiteSpace)
    {
        // Our max padding length will be 7 so add white space as needed
        while (color.length() < 7)
        {
            color += " ";
        }
    }

    // Return the team color with or without the padding
    return color;
}

// Convert an int to a string
static std::string intToString (int number)
{
    std::stringstream string;
    string << number;

    return string.str();
}

// Return whether or not a specified player ID exists or not
static bool isValidPlayerID (int playerID)
{
    // Use another smart pointer so we don't forget about freeing up memory
    std::unique_ptr<bz_BasePlayerRecord> playerData(bz_getPlayerByIndex(playerID));

    // If the pointer doesn't exist, that means the playerID does not exist
    return (playerData) ? true : false;
}

// Convert a string representation of a boolean to a boolean
static bool toBool (std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    std::istringstream is(str);
    bool b;
    is >> std::boolalpha >> b;
    return b;
}

class LeagueOverseer : public bz_Plugin, public bz_CustomSlashCommandHandler, public bz_BaseURLHandler
{
public:
    virtual const char* Name ()
    {
        char buffer[100];
        sprintf(buffer, "League Overseer %i.%i.%i (%i)", MAJOR, MINOR, REV, BUILD);
        return std::string(buffer).c_str();
    }
    virtual void Init (const char* config);
    virtual void Event (bz_EventData *eventData);
    virtual void Cleanup (void);

    virtual bool SlashCommand (int playerID, bz_ApiString, bz_ApiString, bz_APIStringList*);

    virtual void URLDone (const char* URL, const void* data, unsigned int size, bool complete);
    virtual void URLTimeout (const char* URL, int errorCode);
    virtual void URLError (const char* URL, int errorCode, const char *errorString);

    virtual std::string buildBZIDString (bz_eTeamType team);
    virtual void loadConfig (const char *cmdLine);
    virtual void requestTeamName (bz_eTeamType team);
    virtual void requestTeamName (std::string callsign, std::string bzID);
    virtual void validateTeamName (bool &invalidate, bool &teamError, MatchParticipant currentPlayer, std::string &teamName, bz_eTeamType team);

    // We will be storing information about the players who participated in a match so we will
    // be storing that information inside a struct
    struct MatchParticipant
    {
        std::string bzID;
        std::string callsign;
        std::string ipAddress;
        std::string teamName;
        bz_eTeamType teamColor;

        MatchParticipant (std::string _bzID, std::string _callsign, std::string _ipAddress, std::string _teamName, bz_eTeamType _teamColor) :
            bzID(_bzid),
            callsign(_callsign),
            ipAddress(_ipAddress),
            teamName(_teamName),
            teamColor(_teamColor)
        {}
    };

    // Simply out of preference, we will be storing all the information regarding a match inside
    // of a struct where the struct will be NULL if it is current a fun match
    struct OfficialMatch
    {
        bool        playersRecorded,   // Whether or not the players participating in the match have been recorded or not
                    canceled;          // Whether or not the official match was canceled

        std::string cancelationReason, // If the match was canceled, store the reason as to why it was canceled
                    teamOneName,       // We'll be doing our best to store the team names of who's playing so store the names for
                    teamTwoName;       //     each team respectively

        double      startTime,         // The time of the the match was started (in server seconds). Used to calculate roll call
                    duration;          // The length of the match in seconds. Used when reporting a match to the server

        // We keep the number of points scored in the case where all the members of a team leave and their team
        // score get reset to 0
        int         teamOnePoints,
                    teamTwoPoints;

        // We will be storing all of the match participants in this vector
        std::vector<MatchParticipant> matchParticipants;

        // Set the default values for this struct
        OfficialMatch () :
            canceled(false),
            cancelationReason(""),
            teamOneName("Team-A"),
            teamTwoName("Team-B"),
            playersRecorded(false),
            startTime(-1.0f),
            duration(-1.0f),
            teamOnePoints(0),
            teamTwoPoints(0),
            matchParticipants()
        {}
    };

    // All the variables that will be used in the plugin
    bool         ROTATION_LEAGUE, // Whether or not we are watching a league that uses different maps
                 RECORDING;       // Whether or not we are recording a match

    int          DEBUG_LEVEL;     // The DEBUG level the server owner wants the plugin to use for its messages

    double       MATCH_ROLLCALL;  // The number of seconds the plugin needs to wait before recording who's matching

    std::string  LEAGUE_URL,      // The URL the plugin will use to report matches. This should be the URL the PHP counterpart of this plugin
                 MAP_NAME,        // The name of the map that is currently be played if it's a rotation league (i.e. OpenLeague uses multiple maps)
                 MAPCHANGE_PATH;  // The path to the file that contains the name of current map being played

    bz_eTeamType TEAM_ONE,        // Because we're serving more than just GU league, we need to support different colors therefore, call the teams
                 TEAM_TWO;        //     ONE and TWO

    // This is the only pointer of the struct for the official match that we will be using. If this
    // variable is set to NULL, that means that there is currently no official match occurring.
    std::unique_ptr<OfficialMatch> officialMatch;

    // We will be using a map to handle the team name mottos in the format of
    // <BZID, Team Name>
    typedef std::map<std::string, std::string> TeamNameMottoMap;
    TeamNameMottoMap teamMottos;
};

BZ_PLUGIN(LeagueOverseer)

void LeagueOverseer::Init (const char* commandLine)
{
    // Register our events with Register()
    Register(bz_eCaptureEvent);
    Register(bz_eGameEndEvent);
    Register(bz_eGameStartEvent);
    Register(bz_eGetPlayerMotto);
    Register(bz_ePlayerJoinEvent);
    Register(bz_eSlashCommandEvent);
    Register(bz_eTickEvent);

    // Register our custom slash commands
    bz_registerCustomSlashCommand('cancel', this);
    bz_registerCustomSlashCommand('finish', this);
    bz_registerCustomSlashCommand('fm', this);
    bz_registerCustomSlashCommand('official', this);
    bz_registerCustomSlashCommand('spawn', this);
    bz_registerCustomSlashCommand('pause', this);
    bz_registerCustomSlashCommand('resume', this);

    // Set some default values
    MATCH_ROLLCALL = 90;
    officialMatch = NULL;

    // Load the configuration data when the plugin is loaded
    loadConfig(commandLine);

    // Check to see if the plugin is for a rotational league
    if (MAPCHANGE_PATH != "" && ROTATION_LEAGUE)
    {
        // Open the mapchange.out file to see what map is being used
        std::ifstream infile;
        infile.open(MAPCHANGE_PATH.c_str());
        getline(infile, MAP_NAME);
        infile.close();

        //Remove the '.conf' from the mapchange.out file
        MAP_NAME = MAP_NAME.substr(0, MAP_NAME.length() - 5);

        bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Current map being played: %s", MAP_NAME.c_str());
    }

    // Assign our two team colors to eNoTeam simply so we have something to check for
    // when we are trying to find the two colors the map is using
    TEAM_ONE = eNoTeam;
    TEAM_TWO = eNoTeam;

    // Loop through all the team colors
    for (bz_eTeamType t = eRedTeam; t <= ePurpleTeam; t = (bz_eTeamType) (t + 1))
    {
        // If the current team's player limit is more than 0, that means that we found a
        // team color that the map is using
        if (bz_getTeamPlayerLimit(t) > 0)
        {
            // If team one is eNoTeam, then that means this is just the first team with player limit
            // that we have found. If it's not eNoTeam, that means we've found the second team
            if (TEAM_ONE == eNoTeam)
            {
                TEAM_ONE = t;
            }
            else if (TEAM_TWO == eNoTeam)
            {
                TEAM_TWO = t;
                break;
            }
        }
    }

    // Make sure both teams were found, if they weren't then notify in the logs
    ASSERT(TEAM_ONE != eNoTeam && TEAM_TWO != eNoTeam);
}

void LeagueOverseer::Cleanup (void)
{
    Flush(); // Clean up all the events

    // Clean up our custom slash commands
    bz_removeCustomSlashCommand('cancel');
    bz_removeCustomSlashCommand('finish');
    bz_removeCustomSlashCommand('fm');
    bz_removeCustomSlashCommand('official');
    bz_removeCustomSlashCommand('spawn');
    bz_removeCustomSlashCommand('pause');
    bz_removeCustomSlashCommand('resume');
}

void LeagueOverseer::Event (bz_EventData *eventData)
{
    switch (eventData->eventType)
    {
        case bz_eCaptureEvent: // This event is called each time a team's flag has been captured
        {
            // We only need to keep track of the store if it's an official match
            if (officialMatch != NULL)
            {
                bz_CTFCaptureEventData_V1* captureData = (bz_CTFCaptureEventData_V1*)eventData;
                (captureData->teamCapping == TEAM_ONE) ? officialMatch->teamOnePoints++ : officialMatch->teamTwoPoints++;
            }
        }
        break;

        case bz_eGameEndEvent: // This event is called each time a game ends
        {
            // Get the current standard UTC time
            bz_Time standardTime;
            bz_getUTCtime(&standardTime);

            if (officialMatch == NULL)
            {
                // It was a fun match, so there is no need to do anything

                bz_debugMessage(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Fun match has completed.");
            }
            else if (officialMatch->canceled)
            {
                // The match was canceled for some reason so output the reason to both the players and the server logs

                bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: %s", officialMatch->cancelationReason.c_str());
                bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, officialMatch->cancelationReason.c_str());
            }
            else if (officialMatch->matchParticipants.empty())
            {
                // Oops... I darn goofed. Somehow the players were not recorded properly

                bz_debugMessage(DEBUG_LEVEL, "DEBUG :: League Over Seer :: No recorded players for this official match.");
                bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, "Official match could not be reported due to not having a list of valid match participants.");
            }
            else
            {
                // This was an official match, so let's report it

                // Format the date to -> year-month-day hour:minute:second
                char matchDate[20];
                sprintf(matchDate, "%02d-%02d-%02d %02d:%02d:%02d", standardTime.year, standardTime.month, standardTime.day, standardTime.hour, standardTime.minute, standardTime.second);

                // Keep references to values for quick reference
                std::string teamOnePointsFinal = intToString(officialMatch->teamOnePoints);
                std::string teamTwoPointsFinal = intToString(officialMatch->teamTwoPoints);
                std::string matchDuration      = intToString(officialMatch->duration/60);

                // Store match data in the logs
                bz_debugMessagef(0, "Match Data :: League Over Seer Match Report");
                bz_debugMessagef(0, "Match Data :: -----------------------------");
                bz_debugMessagef(0, "Match Data :: Match Time      : %s", matchDate);
                bz_debugMessagef(0, "Match Data :: Duration        : %s", matchDuration.c_str());
                bz_debugMessagef(0, "Match Data :: %s  Score  : %s", formatTeam(TEAM_ONE, true).c_str(), teamOnePointsFinal.c_str());
                bz_debugMessagef(0, "Match Data :: %s  Score  : %s", formatTeam(TEAM_TWO, true).c_str(), teamTwoPointsFinal.c_str());

                // Start building POST data to be sent to the league website
                std::string matchToSend = "query=reportMatch";
                            matchToSend += "&teamOneWins=" + std::string(bz_urlEncode(teamOnePointsFinal.c_str()));
                            matchToSend += "&teamTwoWins=" + std::string(bz_urlEncode(teamTwoPointsFinal.c_str()));
                            matchToSend += "&duration="    + std::string(bz_urlEncode(matchDuration.c_str()));
                            matchToSend += "&matchTime="   + std::string(bz_urlEncode(matchDate));
                            matchToSend += "&server="      + std::string(bz_urlEncode(bz_getPublicAddr().c_str()));
                            matchToSend += "&port="        + std::string(bz_urlEncode(bz_getPublicPort().c_str()));

                // Only add this parameter if it's a rotational league such as OpenLeague
                if (ROTATION_LEAGUE)
                {
                    matchToSend += "&mapPlayed=" + std::string(bz_urlEncode(MAP_NAME.c_str()));
                }

                // Build a string of BZIDs and also output the BZIDs to the server logs while we're at it
                matchToSend += "&teamOnePlayers=" + buildBZIDString(TEAM_ONE);
                matchToSend += "&teamTwoPlayers=" + buildBZIDString(TEAM_TWO);

                // Finish prettifying the server logs
                bz_debugMessagef(0, "Match Data :: -----------------------------");
                bz_debugMessagef(0, "Match Data :: End of Match Report");
                bz_debugMessagef(0, "DEBUG :: League Over Seer :: Reporting match data...");
                bz_sendTextMessage(BZ_SERVER, BZ_ALLUSERS, "Reporting match...");

                //Send the match data to the league website
                bz_addURLJob(LEAGUE_URL.c_str(), this, matchToSend.c_str());
            }

            // Only save the recording buffer if we actually started recording when the match started
            if (RECORDING)
            {
                // We'll be formatting the file name, so create a variable to store it
                char tempRecordingFileName[512];
                std::string recordingFileName;

                // Let's get started with formatting
                if (officialMatch != NULL)
                {
                    // If the official match was finished, then mark it as canceled
                    std::string matchCanceled = (officialMatch->canceled) ? "-Canceled" : "";

                    sprintf(tempRecordingFileName, "Official-%d%02d%02d-%s-vs-%s-%02d%02d%s.rec",
                        standardTime.year, standardTime.month, standardTime.day,
                        officialMatch->teamOneName, officialMatch->teamTwoName,
                        standardTime.hour, standardTime.minute, matchCanceled.c_str());
                }
                else
                {
                    sprintf(tempRecordingFileName, "Fun_Match-%d%02d%02d-%02d%02d.rec",
                        standardTime.year, standardTime.month, standardTime.day,
                        standardTime.hour, standardTime.minute);
                }

                // Move the char[] into a string to handle it better
                recordingFileName = tempRecordingFileName;

                // Save the recording buffer and stop recording
                bz_saveRecBuf(recordingFileName.c_str(), 0);
                bz_stopRecBuf();

                // We're no longer recording, so set the boolean and announce to players that the file has been saved
                RECORDING = false;
                bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Match saved as: %s", recordingFileName.c_str());
            }

            // We're done with the struct, so make it NULL until the next official match
            officialMatch = NULL;
        }
        break;

        case bz_eGameStartEvent: // This event is triggered when a timed game begins
        {
            // We started recording a match, so save the status
            RECORDING = bz_startRecBuf();

            // Check if this is an official match
            if (officialMatch != NULL)
            {
                // Reset scores in case Caps happened during countdown delay.
                officialMatch->teamOnePoints = officialMatch->teamTwoPoints = 0;
                officialMatch->startTime = bz_getCurrentTime();
                officialMatch->duration = bz_getTimeLimit();
            }
        }
        break;

        case bz_eGetPlayerMotto: // This event is called when the player joins. It gives us the motto of the player
        {
            bz_GetPlayerMottoData_V2* mottoData = (bz_GetPlayerMottoData_V2*)eventData;

            mottoData->motto = teamMottos[mottoData->record->bzID.c_str()];
        }
        break;

        case bz_ePlayerJoinEvent: // This event is called each time a player joins the game
        {
            bz_PlayerJoinPartEventData_V1* joinData = (bz_PlayerJoinPartEventData_V1*)eventData;

            // Only notify a player if they exist, have joined the observer team, and there is a match in progress
            if ((bz_isCountDownActive() || bz_isCountDownInProgress()) && isValidPlayerID(joinData->playerID) && joinData->record->team == eObservers)
            {
                bz_sendTextMessagef(BZ_SERVER, joinData->playerID, "*** There is currently %s match in progress, please be respectful. ***",
                                    ((officialMatch != NULL) ? "an official" : "a fun"));
            }

            // Only send a URL job if the user is verified
            if (joinData->record->verified)
            {
                requestTeamName(joinData->record->callsign, joinData->record->bzID);
            }
        }
        break;

        case bz_eSlashCommandEvent: // This event is called each time a player sends a slash command
        {
            bz_SlashCommandEventData_V1* slashCommandData = (bz_SlashCommandEventData_V1*)eventData;

            // Store the information in variables for quick reference
            int         playerID = slashCommandData->from;
            std::string command  = slashCommandData->message.c_str();

            if (strncmp("/gameover", command.c_str(), 9) == 0)
            {
                bz_sendTextMessagef(BZ_SERVER, playerID, "** '/gameover' is disabled, please use /finish or /cancel instead **");
            }
            else if (strncmp("/countdown pause", command.c_str(), 16) == 0)
            {
                bz_sendTextMessagef(BZ_SERVER, playerID, "** '/countdown pause' is disabled, please use /pause instead **");
            }
            else if (strncmp("/countdown resume", command.c_str(), 17) == 0)
            {
                bz_sendTextMessagef(BZ_SERVER, playerID, "** '/countdown resume' is disabled, please use /resume instead **");
            }
            else if (isdigit(atoi(command.c_str()) + 12))
            {
                bz_sendTextMessage(BZ_SERVER, playerID, "** '/countdown TIME' is disabled, please use /official or /fm instead **");
            }
        }
        break;

        case bz_eTickEvent: // This event is called once for each BZFS main loop
        {
            // Get the total number of tanks playing
            int totaltanks = bz_getTeamCount(eRedTeam) + bz_getTeamCount(eGreenTeam) + bz_getTeamCount(eBlueTeam) + bz_getTeamCount(ePurpleTeam);

            // If there are no tanks playing, then we need to do some clean up
            if (totaltanks == 0)
            {
                // If there is an official match and no tanks playing, we need to cancel it
                if (officialMatch != NULL)
                {
                    officialMatch->canceled = true;
                    officialMatch->cancelationReason = "Official match automatically canceled due to all players leaving the match.";
                }

                // If there is a countdown active an no tanks are playing, then cancel it
                if (bz_isCountDownActive())
                {
                    bz_gameOver(253, eObservers);
                }
            }

            // Let's get the roll call only if there is an official match
            if (officialMatch != NULL)
            {
                // Check if the start time is not negative since our default value for the startTime is -1. Also check
                // if it's time to do a roll call, which is defined as 90 seconds after the start of the match by default,
                // and make sure we don't have any match participants recorded
                if (officialMatch->startTime >= 0.0f &&
                    officialMatch->startTime + MATCH_ROLLCALL < bz_getCurrentTime() &&
                    officialMatch->matchParticipants.empty())
                {
                    std::unique_ptr<bz_APIIntList> playerList(bz_getPlayerIndexList());
                    bool invalidateRollcall = teamOneError = teamTwoError = false;
                    std::string teamOneMotto = teamTwoMotto = "";

                    for (unsigned int i = 0; i < playerList->size(); i++)
                    {
                        std::unique_ptr<bz_BasePlayerRecord> playerRecord(bz_getPlayerByIndex(playerList->get(i)));

                        if (bz_getPlayerTeam(playerList->get(i)) != eObservers) //If player is not an observer
                        {
                            MatchParticipant currentPlayer(playerRecord->bzID.c_str(), playerRecord->callsign.c_str(),
                                                           playerRecord->ipAddress.c_str(), teamMottos[mottoData->record->bzID.c_str()],
                                                           playerRecord->team);

                            // Check if there is any need to invalidate a roll call from a team
                            validateTeamName(invalidateRollcall, teamOneError, currentPlayer, teamOneMotto, TEAM_ONE);
                            validateTeamName(invalidateRollcall, teamTwoError, currentPlayer, teamTwoMotto, TEAM_TWO);

                            if (currentPlayer.bzid.empty()) // Someone is playing without a BZID, how did this happen?
                            {
                                invalidateRollcall = true;
                            }

                            // Add the player to the struct of participants
                            officialMatch->matchParticipants.push_back(currentPlayer);
                        }
                    }

                    // We were asked to invalidate the roll call because of some issue so let's check if there is still time for
                    // another roll call
                    if (invalidateRollcall && MATCH_ROLLCALL + 30 < officialMatch->duration)
                    {
                        bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Invalid player found on field at %i:%i.", (int)(MATCH_ROLLCALL/60), (int)(fmod(MATCH_ROLLCALL,60.0)));

                        // There was an error with one of the members of either team, so request a team name update for all of
                        // the team members to try to fix any inconsistencies of different team names
                        if (teamOneError) { requestTeamName(TEAM_ONE); }
                        if (teamTwoError) { requestTeamName(TEAM_TWO); }

                        // Delay the next roll call by 60 seconds
                        MATCH_ROLLCALL += 60;

                        // Clear the struct because it's useless data
                        officialMatch->matchParticipants.clear();
                    }
                }
            }
        }
        break;

        default: break;
    }
}

bool LeagueOverseer::SlashCommand (int playerID, bz_ApiString command, bz_ApiString /*message*/, bz_APIStringList *params)
{
    std::unique_ptr<bz_BasePlayerRecord> playerData(bz_getPlayerByIndex(playerID));

    if (!playerData->verified || !bz_hasPerm(playerID, "spawn"))
    {
        bz_sendTextMessagef(BZ_SERVER, playerID, "You do not have permission to run the /%s command.", command.c_str());
        return true;
    }

    if (command == "cancel")
    {
        if (playerData->team == eObservers) // Observers can't cancel matches
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "Observers are not allowed to cancel matches.");
        }
        else if (bz_isCountDownInProgress())
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "You may only cancel a match after it has started.");
        }
        else if (bz_isCountDownActive()) // Cannot cancel during countdown before match
        {
            if (officialMatch != NULL)
            {
                officialMatch->canceled = true;
                officialMatch->cancelationReason = "Official match cancellation requested by " + std::string(playerData->callsign.c_str());
            }
            else
            {
                bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Fun match ended by %s", playerData->callsign.c_str());
            }

            bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Match ended by %s (%s).", playerData->callsign.c_str(), playerData->ipAddress.c_str());
            bz_gameOver(253, eObservers);
        }
        else
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "There is no match in progress to cancel.");
        }

        return true;
    }
    else if (command == "finish")
    {
        if (playerData->team == eObservers) // Observers can't cancel matches
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "Observers are not allowed to cancel matches.");
        }
        else if (bz_isCountDownInProgress())
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "You may only cancel a match after it has started.");
        }
        else if (bz_isCountDownActive())
        {
            // We can only '/finish' official matches because I wanted to have a command only dedicated to
            // reporting partially completed matches
            if (officialMatch != NULL)
            {
                // Let's check if we can report the match, in other words, at least half of the match has been reported
                if (officialMatch->startTime >= 0.0f && officialMatch->startTime + (officialMatch->duration/2) < bz_getCurrentTime())
                {
                    bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: Match Over Seer :: Official match ended early by %s (%s)", playerData->callsign.c_str(), playerData->ipAddress.c_str());
                    bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Official match ended early by %s", playerData->callsign.c_str());

                    bz_gameOver(253, eObservers);
                }
                else
                {
                    bz_sendTextMessage(BZ_SERVER, playerID, "Sorry, I cannot automatically report a match less than half way through.");
                    bz_sendTextMessage(BZ_SERVER, playerID, "Please use the /cancel command and message a referee for review of this match.");
                }
            }
            else
            {
                bz_sendTextMessage(BZ_SERVER, playerID, "You cannot /finish a fun match. Use /cancel instead.");
            }
        }
        else
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "There is no match in progress to end.");
        }

        return true;
    }
    else if (command == "fm")
    {
        if (playerData->team == eObservers) //Observers can't start matches
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "Observers are not allowed to start matches.");
        }
        else if (match || bz_isCountDownActive() || bz_isCountDownInProgress()) //There is already a countdown
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "There is already a game in progress; you cannot start another.");
        }
        else //They are verified, not an observer, there is no match so start one!
        {
            officialMatch = NULL;

            bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Fun match started by %s (%s).", playerData->callsign.c_str(), playerData->ipAddress.c_str());
            bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Fun match started by %s.", playerData->callsign.c_str());

            int timeToStart = (params->size() == 1) ? atoi(params->get(0).c_str()) : 10;

            if (timeToStart <= 120 && timeToStart >= 5)
            {
                bz_startCountdown (timeToStart, bz_getTimeLimit(), "Server"); //Start the countdown with a custom countdown time limit under 2 minutes
            }
            else
            {
                bz_startCountdown (10, bz_getTimeLimit(), "Server"); //Start the countdown for the official match
            }
        }

        return true;
    }
    else if (command == "official")
    {
        if (playerData->team == eObservers) //Observers can't start matches
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "Observers are not allowed to start matches.");
        }
        else if (bz_getTeamCount(teamOne) < 2 || bz_getTeamCount(teamTwo) < 2) //An official match cannot be 1v1 or 2v1
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "You may not have an official match with less than 2 players per team.");
        }
        else if (officialMatch != NULL || bz_isCountDownActive() || bz_isCountDownInProgress()) //A countdown is in progress already
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "There is already a game in progress; you cannot start another.");
        }
        else //They are verified non-observer with valid team sizes and no existing match. Start one!
        {
            officialMatch.reset(new OfficialMatch()); //It's an official match

            bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Official match started by %s (%s).", playerData->callsign.c_str(), playerData->ipAddress.c_str());
            bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "Official match started by %s.", playerData->callsign.c_str());

            int timeToStart = (params->size() == 1) ? atoi(params->get(0).c_str()) : 10;

            if (timeToStart <= 120 && timeToStart >= 5)
            {
                bz_startCountdown (timeToStart, bz_getTimeLimit(), "Server"); //Start the countdown with a custom countdown time limit under 2 minutes
            }
            else
            {
                bz_startCountdown (10, bz_getTimeLimit(), "Server"); //Start the countdown for the official match
            }
        }

        return true;
    }
    else if (command == "pause")
    {
        if (bz_isCountDownPaused())
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "The match is already paused!");
        }
        else if (bz_isCountDownActive())
        {
            bz_pauseCountdown(playerData->callsign.c_str());
        }
        else
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "There is no active match to pause right now.");
        }

        return true;
    }
    else if (command == "resume")
    {
        if (!bz_isCountDownPaused())
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "The match is not paused!");
        }
        else if (bz_isCountDownActive())
        {
            bz_resumeCountdown(playerData->callsign.c_str());
        }
        else
        {
            bz_sendTextMessage(BZ_SERVER, playerID, "There is no active match to resume right now.");
        }

        return true;
    }
    else if (command == "spawn")
    {
        if (bz_hasPerm(playerID, "ban"))
        {
            if (params->size() > 0)
            {
                std::string callsignToLookup; // Store the callsign we're going to search for

                for (unsigned int i = 0; i < params->size(); i++) // Piece together the callsign from the slash command parameters
                {
                    callsignToLookup += params->get(i).c_str();

                    if (i != params->size() - 1) // So we don't stick a whitespace on the end
                    {
                        callsignToLookup += " "; // Add a whitespace between each chat text parameter
                    }
                }

                if (std::string::npos != std::string(params->get(0).c_str()).find("#") && isValidPlayerID(atoi(std::string(params->get(0).c_str()).erase(0, 1).c_str())))
                {
                    int victimPlayerID = atoi(std::string(params->get(0).c_str()).erase(0, 1).c_str());
                    std::unique_ptr<bz_BasePlayerRecord> victim(bz_getPlayerByIndex(victimPlayerID));

                    bz_grantPerm(victim->playerID, "spawn");
                    bz_sendTextMessagef(BZ_SERVER, eAdministrators, "%s granted %s the ability to spawn.", playerData->callsign.c_str(), victim->callsign.c_str());
                }
                else if (bz_getPlayerByCallsign(callsignToLookup) != NULL)
                {
                    std::unique_ptr<bz_BasePlayerRecord> victim(bz_getPlayerByCallsign(callsignToLookup));

                    bz_grantPerm(victim->playerID, "spawn");
                    bz_sendTextMessagef(BZ_SERVER, eAdministrators, "%s granted %s the ability to spawn.", playerData->callsign.c_str(), victim->callsign.c_str());
                }
                else
                {
                    bz_sendTextMessagef(BZ_SERVER, playerID, "player %s not found", params->get(0).c_str());
                }
            }
            else
            {
                bz_sendTextMessage(BZ_SERVER, playerID, "/spawn <player id or callsign>");
            }
        }
        else if (!playerData->admin)
        {
            bz_sendTextMessage(BZ_SERVER,playerID,"You do not have permission to use the /spawn command.");
        }

        return true;
    }
}

// Everything went fine with the report
void LeagueOverSeer::URLDone(const char* /*URL*/, const void* data, unsigned int /*size*/, bool /*complete*/)
{
    std::string siteData = (const char*)(data); // Convert the data to a std::string
    bz_debugMessagef(DEBUG_LEVEL, "URL Job Successful! Data returned: %s", siteData.c_str());

    if (/* check for proper JSON */)
    {
        // TODO: Handle JSON stuff
    }
    else if (siteData.find("<html>") == std::string::npos)
    {
        bz_sendTextMessagef(BZ_SERVER, BZ_ALLUSERS, "%s", siteData.c_str());
        bz_debugMessagef(DEBUG_LEVEL, "%s", siteData.c_str());
    }
}

// The league website is down or is not responding, the request timed out
void LeagueOverSeer::URLTimeout(const char* /*URL*/, int /*errorCode*/)
{
    bz_debugMessage(DEBUG_LEVEL, "DEBUG :: League Over Seer :: The request to the league site has timed out.");
}

// The server owner must have set up the URLs wrong because this shouldn't happen
void LeagueOverSeer::URLError(const char* /*URL*/, int errorCode, const char *errorString)
{
    bz_debugMessage(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Match report failed with the following error:");
    bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Error code: %i - %s", errorCode, errorString);
}

// We are building a string of BZIDs from the people who matched in the match that just occurred
// and we're also writing the player information to the server logs while we're at it. Efficiency!
std::string LeagueOverSeer::buildBZIDString (bz_eTeamType team)
{
    // The string of BZIDs separated by commas
    std::string teamString;

    // Send a debug message of the players on the specified team
    bz_debugMessagef(0, "Match Data :: %s Team Players", formatTeam(team, false).c_str());

    // Add all the players from the specified team to the match report
    for (unsigned int i = 0; i < officialMatch->matchParticipants.size(); i++)
    {
        // If the player current player is part of the team we're formatting
        if (officialMatch->matchParticipants.at(i).teamColor == team)
        {
            // Add the BZID of the player to string with a comma at the end
            teamString += std::string(bz_urlEncode(officialMatch->matchParticipants.at(i).bzid.c_str())) + ",";

            // Output their information to the server logs
            bz_debugMessagef(0, "Match Data ::  %s [%s] (%s)", officialMatch->matchParticipants.at(i).callsign.c_str(),
                                                               officialMatch->matchParticipants.at(i).bzid.c_str(),
                                                               officialMatch->matchParticipants.at(i).ipAddress.c_str());
        }
    }

    // Return the comma separated string minus the last character because the loop will always
    // add an extra comma at the end. If we leave it, it will cause issues with the PHP counterpart
    // which tokenizes the BZIDs by commas and we don't want an empty BZID
    return teamString.erase(teamString.size() - 1);
}

// Load the plugin configuration file
void LeagueOverSeer::loadConfig(const char* cmdLine)
{
    PluginConfig config = PluginConfig(cmdLine);
    std::string section = "leagueOverSeer";

    // Shutdown the server if the configuration file has errors because we can't do anything
    // with a broken config
    if (config.errors) { bz_shutdown(); }

    // Extract all the data in the configuration file and assign it to plugin variables
    ROTATION_LEAGUE = toBool(config.item(section, "ROTATIONAL_LEAGUE"));
    mapchangePath   = config.item(section, "MAPCHANGE_PATH");
    LEAGUE_URL      = config.item(section, "LEAGUE_OVER_SEER_URL");
    DEBUG_LEVEL     = atoi((config.item(section, "DEBUG_LEVEL")).c_str());

    // Check for errors in the configuration data. If there is an error, shut down the server
    if (strcmp(LEAGUE_URL.c_str(), "") == 0)
    {
        bz_debugMessage(0, "*** DEBUG :: League Over Seer :: No URLs were choosen to report matches or query teams. ***");
        bz_shutdown();
    }
    if (DEBUG_LEVEL > 4 || DEBUG_LEVEL < 0)
    {
        bz_debugMessage(0, "*** DEBUG :: League Over Seer :: Invalid debug level in the configuration file. ***");
        bz_shutdown();
    }
}

// Request a team name update for all the members of a team
void LeagueOverSeer::requestTeamName (bz_eTeamType team)
{
    std::unique_ptr<bz_APIIntList> playerList(bz_getPlayerIndexList());

    for (unsigned int i = 0; i < playerList->size(); i++)
    {
        std::unique_ptr<bz_BasePlayerRecord> playerRecord(bz_getPlayerByIndex(playerList->get(i)));

        if (playerRecord->team == team) // Only request a new team name for the players of a certain team
        {
            requestTeamName(playerRecord->callsign, playerRecord->bzID);
        }
    }
}

// Because there will be different times where we request a team name motto, let's make into a function
void LeagueOverSeer::requestTeamName (std::string callsign, std::string bzID)
{
    // Build the POST data for the URL job
    std::string teamMotto = "query=teamNameQuery";
    teamMotto += "&teamPlayers=" + std::string(bzID.c_str());

    bz_debugMessagef(DEBUG_LEVEL, "DEBUG :: League Over Seer :: Getting motto for %s...", callsign.c_str());

    // Send the team update request to the league website
    bz_addURLJob(LEAGUE_URL.c_str(), this, teamMotto.c_str());
}

// Check if there is any need to invalidate a roll call team
void LeagueOverSeer::validateTeamName (bool &invalidate, bool &teamError, MatchParticipant currentPlayer, std::string &teamName, bz_eTeamType team)
{
    // Check if the player is a part of team one
    if (currentPlayer.teamColor == team)
    {
        // Check if the team name of team one has been set yet, if it hasn't then set it
        // and we'll be able to set it so we can conclude that we have the same team for
        // all of the players
        if (teamName == "")
        {
            teamName = currentPlayer.teamName;
        }
        // We found someone with a different team name, therefore we need invalidate the
        // roll call and check all of the member's team names for sanity
        else if (teamName != currentPlayer.teamName)
        {
            invalidate = true; // Invalidate the roll call
            teamError = true;  // We need to check team one's members for their teams
        }
    }
}