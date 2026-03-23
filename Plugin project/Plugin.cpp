#include <windows.h>
#include <tchar.h>
#include <cstring>

#include "EuroScopePlugIn.h"
#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>

using namespace EuroScopePlugIn;

// ================= DATA STRUCTURES =================

struct Sector
{
    int lower;
    int upper;
    std::vector<std::string> owners;
};

struct COPX
{
    std::string fixBefore;
    std::string dep;
    std::string depRwy;

    std::string copx;
    std::string fix;
    std::string fixAfter;

    std::string arr;
    std::string arrRwy;

    std::string from;
    std::string to;

    std::string climb;
    int descend;

    std::string name;
};

// ================= GLOBAL STORAGE =================

std::map<std::string, Sector> sectors;
std::map<std::string, std::string> positionToID;
std::vector<COPX> copxList;

// Global plugin pointer (use base type so it's usable before CLevelPlugin is defined)
extern CPlugIn* g_pPlugin;

// ================= HELPERS =================

int ToFL(int alt) { return alt / 100; }

// ================= ESE LOADING =================

void LoadESE()
{
    std::ifstream file("C:\\Users\\jwoom\\Documents\\Euroscope EDYY\\Sector File\\belux.ese"); // CHANGE THIS
    std::string line;

    bool inPositions = false;

    Sector currentSector;
    std::string currentSectorName = "";

    while (std::getline(file, line))
    {
        // -------- POSITIONS --------
        if (line.find("[POSITIONS]") != std::string::npos)
        {
            inPositions = true;
            continue;
        }
        if (line.size() > 0 && line[0] == '[' && line.find("[POSITIONS]") == std::string::npos)
        {
            inPositions = false;
        }

        if (inPositions)
        {
            std::stringstream ss(line);
            std::string part;
            std::vector<std::string> parts;

            while (std::getline(ss, part, ':'))
                parts.push_back(part);

            if (parts.size() > 3)
            {
                positionToID[parts[0]] = parts[3];
            }
        }

        // -------- SECTORS --------
        if (line.find("SECTOR:") == 0)
        {
            std::stringstream ss(line);
            std::string part;
            std::vector<std::string> parts;

            while (std::getline(ss, part, ':'))
                parts.push_back(part);

            if (parts.size() > 3)
            {
                currentSectorName = parts[1];
                currentSector.lower = ToFL(atoi(parts[2].c_str()));
                currentSector.upper = ToFL(atoi(parts[3].c_str()));
                currentSector.owners.clear();
            }
        }
        else if (line.find("OWNER:") == 0 && currentSectorName != "")
        {
            std::stringstream ss(line);
            std::string part;

            std::getline(ss, part, ':'); // skip OWNER

            while (std::getline(ss, part, ':'))
                currentSector.owners.push_back(part);

            sectors[currentSectorName] = currentSector;
            currentSectorName = "";
        }

        // -------- COPX --------
        if (line.find("COPX:") == 0)
        {
            std::stringstream ss(line);
            std::string part;
            std::vector<std::string> parts;

            while (std::getline(ss, part, ':'))
                parts.push_back(part);

            if (parts.size() >= 13)
            {
                COPX c;

                c.fixBefore = parts[1];
                c.dep = parts[2];
                c.depRwy = parts[3];

                c.copx = parts[4];
                c.fix = parts[5];
                c.fixAfter = parts[6];

                c.arr = parts[7];
                c.arrRwy = parts[8];

                c.from = parts[9];
                c.to = parts[10];

                c.climb = parts[11];
                c.descend = ToFL(atoi(parts[12].c_str()));

                if (parts.size() > 13)
                    c.name = parts[13];

                copxList.push_back(c);
            }
        }
    }
}

// ================= CONTROLLER LOGIC =================

std::string GetPositionID()
{
    if (!g_pPlugin)
        return "";

    std::string callsign = g_pPlugin->ControllerMyself().GetPositionId();

    if (positionToID.find(callsign) != positionToID.end())
        return positionToID[callsign];

    return "";
}

std::vector<std::string> GetOnlinePositionIDs()
{
    std::vector<std::string> result;

    if (!g_pPlugin)
        return result;

    CController c = g_pPlugin->ControllerSelectFirst();

    while (c.IsValid())
    {
        std::string cs = c.GetCallsign();

        if (positionToID.find(cs) != positionToID.end())
            result.push_back(positionToID[cs]);

        c = g_pPlugin->ControllerSelectNext(c);
    }

    return result;
}

std::string GetSectorOwner(const Sector& sector)
{
    std::vector<std::string> online = GetOnlinePositionIDs();

    for (const auto& owner : sector.owners)
    {
        for (const auto& o : online)
        {
            if (owner == o)
                return owner;
        }
    }

    return "";
}

std::vector<std::string> GetOwnSectors()
{
    std::vector<std::string> result;
    std::string myID = GetPositionID();

    for (auto& s : sectors)
    {
        if (GetSectorOwner(s.second) == myID)
            result.push_back(s.first);
    }

    return result;
}

// ================= PLUGIN =================

class CLevelPlugin : public CPlugIn
{
public:
    CLevelPlugin()
        : CPlugIn(COMPATIBILITY_CODE, "LevelPlugin", "2.0", "You", "TFL plugin")
    {
        RegisterTagItemType("LVL", 1);
        LoadESE();
    }

    void OnGetTagItem(CFlightPlan fp, CRadarTarget rt,
        int ItemCode, int TagData,
        char sItemString[16], int* pColor,
        COLORREF* pRGB, double* pFontSize)
    {
        if (ItemCode != 1 || !fp.IsValid())
        {
            strcpy_s(sItemString, 16, "---");
            return;
        }

        int rfl = ToFL(fp.GetFinalAltitude());

        std::string route = fp.GetFlightPlanData().GetRoute();
        std::string dep = fp.GetFlightPlanData().GetOrigin();
        std::string dest = fp.GetFlightPlanData().GetDestination();

        std::vector<std::string> ownSectors = GetOwnSectors();

        int tfl = 0;
        std::string suffix = "";
        std::string usedSector = "";

        // -------- COPX MATCHING --------
        for (auto& c : copxList)
        {
            bool fromMatch = false;

            for (auto& own : ownSectors)
            {
                if (c.from == own)
                {
                    fromMatch = true;
                    usedSector = own;
                    break;
                }
            }

            if (!fromMatch) continue;
            if (c.dep != "*" && c.dep != dep) continue;
            if (c.arr != "*" && c.arr != dest) continue;
            if (route.find(c.fix) == std::string::npos) continue;
            if (c.fixBefore != "*" && route.find(c.fixBefore) == std::string::npos) continue;
            if (c.fixAfter != "*" && route.find(c.fixAfter) == std::string::npos) continue;

            tfl = c.descend;

            if (sectors.count(c.from) && sectors.count(c.to))
            {
                Sector own = sectors[c.from];
                Sector next = sectors[c.to];

                if (next.lower >= own.upper) suffix = "U";
                else if (next.upper <= own.lower) suffix = "L";
                else suffix = "N";
            }

            break;
        }

        // -------- FRA FALLBACK --------
        if (tfl == 0)
        {
            for (auto& own : ownSectors)
            {
                Sector s = sectors[own];

                if (rfl > s.upper)
                {
                    tfl = s.upper;
                    suffix = "U";
                    usedSector = own;
                }
                else if (rfl < s.lower)
                {
                    tfl = s.lower;
                    suffix = "L";
                    usedSector = own;
                }
            }
        }

        // -------- CROSSING LOGIC --------
        bool isCrossing = false;

        if (sectors.count(usedSector))
        {
            Sector s = sectors[usedSector];

            if (rfl > s.upper && tfl < s.upper)
                isCrossing = true;
        }

        // -------- OUTPUT --------
        std::string result;

        if (isCrossing)
            result = std::to_string(rfl) + "X" + std::to_string(tfl);
        else if (tfl > 0)
            result = std::to_string(tfl) + suffix;
        else
            result = "---";

        strcpy_s(sItemString, 16, result.c_str());
    }
};

// ================= EXPORT =================

// Define the global plugin instance pointer (base type)
CPlugIn* g_pPlugin = nullptr;

extern "C" __declspec(dllexport)
EuroScopePlugIn::CPlugIn* __cdecl EuroScopePlugInInit()
{
    g_pPlugin = new CLevelPlugin();
    return g_pPlugin;
}
