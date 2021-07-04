/*
 * ReaKontrol
 * Support for MIDI protocol used by Komplete Kontrol S-series Mk2, A-series
 * and M-Series
 * Author: brumbear@pacificpeaks
 * Copyright 2019-2020 Pacific Peaks Studio
 * Previous Authors: James Teh <jamie@jantrid.net>, Leonard de Ruijter, brumbear@pacificpeaks, Copyright 2018-2019 James Teh
 * License: GNU General Public License version 2.0
 */

// #define CALLBACK_DIAGNOSTICS
// #define CONNECTION_DIAGNOSTICS
// #define DEBUG_DIAGNOSTICS
// #define BASIC_DIAGNOSTICS

#include <string>
#include <sstream>
#include <cstring>
#include <vector>
#include "reaKontrol.h"

using namespace std;

const unsigned char MIDI_CC = 0xBF;
const unsigned char MIDI_SYSEX_BEGIN[] = {
	0xF0, 0x00, 0x21, 0x09, 0x00, 0x00, 0x44, 0x43, 0x01, 0x00};
const unsigned char MIDI_SYSEX_END = 0xF7;
const int BANK_NUM_TRACKS = 8;

const unsigned char CMD_HELLO = 0x01;
const unsigned char CMD_GOODBYE = 0x02;
const unsigned char CMD_PLAY = 0x10;
const unsigned char CMD_RESTART = 0x11;
const unsigned char CMD_REC = 0x12; // ExtEdit: Toggle record arm for selected track
const unsigned char CMD_COUNT = 0x13;
const unsigned char CMD_STOP = 0x14;
const unsigned char CMD_CLEAR = 0x15; // ExtEdit: Remove Selected Track
const unsigned char CMD_LOOP = 0x16; // ExtEdit: Change right edge of time selection +/- 1 beat length
const unsigned char CMD_METRO = 0x17; // ExtEdit: Change project tempo in 1 bpm steps decrease/increase
const unsigned char CMD_TEMPO = 0x18;
const unsigned char CMD_UNDO = 0x20;
const unsigned char CMD_REDO = 0x21;
const unsigned char CMD_QUANTIZE = 0x22;
const unsigned char CMD_AUTO = 0x23;
const unsigned char CMD_NAV_TRACKS = 0x30;
const unsigned char CMD_NAV_BANKS = 0x31;
const unsigned char CMD_NAV_CLIPS = 0x32;
const unsigned char CMD_NAV_SCENES = 0x33; // not used in NIHIA?
const unsigned char CMD_MOVE_TRANSPORT = 0x34;
const unsigned char CMD_MOVE_LOOP = 0x35;
const unsigned char CMD_TRACK_AVAIL = 0x40;
const unsigned char CMD_SET_KK_INSTANCE = 0x41;
const unsigned char CMD_TRACK_SELECTED = 0x42;
const unsigned char CMD_TRACK_MUTED = 0x43;
const unsigned char CMD_TRACK_SOLOED = 0x44;
const unsigned char CMD_TRACK_ARMED = 0x45;
const unsigned char CMD_TRACK_VOLUME_TEXT = 0x46;
const unsigned char CMD_TRACK_PAN_TEXT = 0x47;
const unsigned char CMD_TRACK_NAME = 0x48;
const unsigned char CMD_TRACK_VU = 0x49;
const unsigned char CMD_TRACK_MUTED_BY_SOLO = 0x4A;
const unsigned char CMD_KNOB_VOLUME0 = 0x50;
const unsigned char CMD_KNOB_VOLUME1 = 0x51;
const unsigned char CMD_KNOB_VOLUME2 = 0x52;
const unsigned char CMD_KNOB_VOLUME3 = 0x53;
const unsigned char CMD_KNOB_VOLUME4 = 0x54;
const unsigned char CMD_KNOB_VOLUME5 = 0x55;
const unsigned char CMD_KNOB_VOLUME6 = 0x56;
const unsigned char CMD_KNOB_VOLUME7 = 0x57;
const unsigned char CMD_KNOB_PAN0 = 0x58;
const unsigned char CMD_KNOB_PAN1 = 0x59;
const unsigned char CMD_KNOB_PAN2 = 0x5a;
const unsigned char CMD_KNOB_PAN3 = 0x5b;
const unsigned char CMD_KNOB_PAN4 = 0x5c;
const unsigned char CMD_KNOB_PAN5 = 0x5d;
const unsigned char CMD_KNOB_PAN6 = 0x5e;
const unsigned char CMD_KNOB_PAN7 = 0x5f;
const unsigned char CMD_PLAY_CLIP = 0x60; // ExtEdit: Insert track
const unsigned char CMD_STOP_CLIP = 0x61; // Enter extEditMode
const unsigned char CMD_PLAY_SCENE = 0x62; // not used in NIHIA?
const unsigned char CMD_RECORD_SESSION = 0x63; // not used in NIHIA?
const unsigned char CMD_CHANGE_SEL_TRACK_VOLUME = 0x64;
const unsigned char CMD_CHANGE_SEL_TRACK_PAN = 0x65;
const unsigned char CMD_TOGGLE_SEL_TRACK_MUTE = 0x66; // Attention(!): NIHIA 1.8.7 used SysEx, NIHIA 1.8.8 uses Cc
const unsigned char CMD_TOGGLE_SEL_TRACK_SOLO = 0x67; // Attention(!): NIHIA 1.8.7 used SysEx, NIHIA 1.8.8 uses Cc
const unsigned char CMD_SEL_TRACK_AVAILABLE = 0x68; // Attention(!): NIHIA 1.8.7 used SysEx, NIHIA 1.8.8 uses Cc
const unsigned char CMD_SEL_TRACK_MUTED_BY_SOLO = 0x69; // Attention(!): NIHIA 1.8.7 used SysEx, NIHIA 1.8.8 uses Cc

const unsigned char TRTYPE_UNSPEC = 1;
const unsigned char TRTYPE_MIDI = 2;
const unsigned char TRTYPE_AUDIO = 3;
const unsigned char TRTYPE_GROUP = 4;
const unsigned char TRTYPE_BUS = 5;
const unsigned char TRTYPE_MASTER = 6;

const bool HIDE_MUTED_BY_SOLO = false; // Meter Setting: If TRUE peak levels will not be shown in Mixer view of muted by solo tracks. If FALSE they will be shown but greyed out.
const int FLASH_T = 16; // value devided by 30 -> button light flash interval time in Extended Edit Mode
const int CYCLE_T = 8; // value devided by 30 -> 4D encoder LED cycle interval time in Extended Edit Mode
const int SCAN_T = 90; // value devided by 30 -> Scan interval time to check for Komplete Kontrol MIDI device plugged in (hot plugging)
const int CONNECT_N = 2; // number of connection attempts to switch detected keyboard to NiMidi Mode

#define CSURF_EXT_SETMETRONOME 0x00010002

// State Information (Cache)
// ToDo: Rather than using glocal variables consider moving these into the BaseSurface class declaration
static int g_trackInFocus = -1;
static bool g_anySolo = false;
static int g_soloStateBank[BANK_NUM_TRACKS] = { 0 };
static bool g_muteStateBank[BANK_NUM_TRACKS] = { false };

static bool g_KKcountInTriggered = false; // to discriminate if COUNT IN was requested by keyboard or from Reaper
static int g_KKcountInMetroState = 0; // to store metronome state when COUNT IN was requested by keyboard

// Extended Edit Control State Variables
const int EXT_EDIT_OFF = 0; // no Extended Edit, Normal Mode. flashTimer = -1 
const int EXT_EDIT_ON = 1; // Extended Edit 1st stage commands
const int EXT_EDIT_LOOP = 2; // Extended Edit LOOP
const int EXT_EDIT_TEMPO = 3; // Extended Edit TEMPO
const int EXT_EDIT_ACTIONS = 4; // Extended Edit ACTIONS. flashTimer = -4
static int g_extEditMode = EXT_EDIT_OFF;

// Connection Status State Variables
const int KK_NOT_CONNECTED = 0; // not connected / scanning
const int KK_MIDI_FOUND = 1; // KK MIDI device found / trying to connect to NIHIA
const int KK_NIHIA_CONNECTED = 2; // NIHIA HELLO acknowledged / fully connected
static int g_connectedState = KK_NOT_CONNECTED; 

# ifdef CONNECTION_DIAGNOSTICS
static int log_scanAttempts = 0;
static int log_connectAttempts = 0;
# endif

// Global action list structure for ReaKontrol
const int A_NAME_MAX = 128;
struct aList {
	int ID[8];
	char name[8][A_NAME_MAX];
};
aList g_actionList;
bool g_actionListLoaded = false; // action list will be populated from ini file after successful connection to allow Reaper main thread to load all extensions first

#ifdef __APPLE__
    #define strcpy_s(dest,dest_sz, src) strlcpy(dest,src, dest_sz)
    #define REAKONTROL_INI "/UserPlugins/ReaKontrolConfig/reakontrol.ini"
#else
    #define REAKONTROL_INI "\\UserPlugins\\ReaKontrolConfig\\reakontrol.ini"
#endif

const char *kk_device_names[] {
    "Komplete Kontrol DAW - 1",
    "Komplete Kontrol A DAW",
    "Komplete Kontrol M DAW",
    NULL
};

void loadActionList() {
	// Load Global Action list from config file
	const char* pathname = GetResourcePath();
	std::string s_filename(pathname);
	s_filename += REAKONTROL_INI;
	s_filename.push_back('\0');
	pathname = &s_filename[0];
	if (file_exists(pathname)) {
		std::string s_keyName;
		char* key;
		char stringOut[A_NAME_MAX] = {}; // large enough for the longest possible action ID or action name
		int stringOut_sz = sizeof(stringOut);

		for (int i = 0; i <= 7; ++i) {
			s_keyName = "action_" + std::to_string(i) + "_ID";
			s_keyName.push_back('\0');
			key = &s_keyName[0];
			GetPrivateProfileString("reakontrol_actions", key, nullptr, &stringOut[0], stringOut_sz, pathname); // Windows only. Mac OSX via Swell version of this?
			if (stringOut[0] != '\0') {
				g_actionList.ID[i] = NamedCommandLookup(&stringOut[0]);
				if (g_actionList.ID[i]) {
					s_keyName = "action_" + std::to_string(i) + "_name";
					s_keyName.push_back('\0');
					key = &s_keyName[0];
					GetPrivateProfileString("reakontrol_actions", key, nullptr, &stringOut[0], stringOut_sz, pathname); // Windows only. Mac OSX via Swell version of this?
					if (stringOut[0] != '\0') {
						strcpy_s(&g_actionList.name[i][0], A_NAME_MAX, &stringOut[0]);
					}
					else {
						g_actionList.name[i][0] = '\0';
					}
				}
			}
			else {
				g_actionList.ID[i] = 0;
				g_actionList.name[i][0] = '\0';
			}

		}

#ifdef DEBUG_DIAGNOSTICS
		ostringstream s;
		for (int i = 0; i <= 7; ++i) {
			if (g_actionList.ID[i] != 0) {
				s.str("");
				s << "Action " << i << " " << g_actionList.ID[i] << " ";
				ShowConsoleMsg(s.str().c_str());
				if (g_actionList.name[i][0] != '\0') {
					ShowConsoleMsg(g_actionList.name[i]);
				}
				ShowConsoleMsg("\n");
			}
		}
#endif

	}
	else {
		ostringstream s;
		s << "Komplete Kontrol Keyboard successfully connected but configuration file not found! Please read manual how to configure custom actions via the configuration file:"
			<< endl << endl
			<< s_filename;
		ShowMessageBox(s.str().c_str(), "ReaKontrol", 0);
		g_actionList.ID[0] = -1;
	}
}

int getKkMidiInput() {
    int count = GetNumMIDIInputs();
    for (int dev = 0; dev < count; ++dev) {
        char name[60];
        bool present = GetMIDIInputName(dev, name, sizeof(name));
        if (!present) {
            continue;
        }

        for (int i = 0; kk_device_names[i] != NULL; i++) {
            if (strstr(name, kk_device_names[i]) != NULL) {
                return dev;
            }
        }
    }
    return -1;
}

int getKkMidiOutput() {
    int count = GetNumMIDIOutputs();
    for (int dev = 0; dev < count; ++dev) {
        char name[60];
        bool present = GetMIDIOutputName(dev, name, sizeof(name));
        if (!present) {
            continue;
        }

        for (int i = 0; kk_device_names[i] != NULL; i++) {
            if (strstr(name, kk_device_names[i]) != NULL) {
                return dev;
            }
        }
    }
    return -1;
}

signed char convertSignedMidiValue(unsigned char value) {
	// Convert a signed 7 bit MIDI value to a signed char.
	// That is, convertSignedMidiValue(127) will return -1.
	if (value <= 63) {
		return value;
	}
	return value - 128;
}

static unsigned char panToChar(double pan){
	pan = (pan + 1.0)*63.5;
	if (pan < 0.0)pan = 0.0;
	else if (pan > 127.0)pan = 127.0;
	return (unsigned char)(pan + 0.5);
}

static unsigned char volToChar_KkMk2(double volume) {
	// Direct linear logarithmic conversion to KK Mk2 Meter Scaling.
	// Contrary to Reaper's Peak Volume Meters the dB interval spacing on KK Mk2 displays is NOT linear.
	// It is assumed that other NI Keyboards use the same scaling for the meters.
	// Peak: Midi #127 = +6dB #106 = 0dB, #68 = -12dB, #38 = -24dB, #16 = -48dB, #2 = -96dB, #1 = -infinite
	// VolMarker: Midi #127 = +6dB #109 = 0dB, #68 = -12dB, #38 = -24dB, #12 = -48dB, #2 = -96dB, #0 = -infinite
	constexpr double minus48dB = 0.00398107170553497250;
	constexpr double minus96dB = 1.5848931924611134E-05;
	constexpr double m = (16.0 - 2.0) / (minus48dB - minus96dB);
	constexpr double n = 16.0 - m * minus48dB;
	constexpr double a = -3.2391538612390192E+01;
	constexpr double b = 3.0673618643561021E+01;
	constexpr double c = 8.6720798984917224E+01;
	constexpr double d = 4.4920143012996103E+00;
	double result = 0.0;
	if (volume > minus48dB) {
		result = a + b * log(c * volume + d); // KK Mk2 display has meaningful resolution only above -48dB
	}
	else if (volume > minus96dB) {
		result = m * volume + n; // linear approximation between -96dB and -48dB
	}
	else {
		result = 0.5; // will be rounded to 1
	}
	if (result > 126.5) {
		result = 126.5;
	}
	return (unsigned char)(result + 0.5); // rounding and make sure that minimum value returned is 1
}

// ==============================================================================================================================
// Architectural Note: Callback vs polling Architecture. The approach here is to use a callback architecture when possible and poll only if
// callbacks are not available or behave inefficiently or do not report reliably
// Some Reaper callbacks (Set....) are called unecessarily (?) often in Reaper, see various forum reports & comments from schwa
// Notably this thread: https://forum.cockos.com/showthread.php?t=49536
// => Implement state checking / looking for changes at beginning of every callback to return immediately if call is not necessary -> save CPU
// ==============================================================================================================================

// ToDo: NIHIA shows a strange behaviour: The mixer view is sometimes not updated completely until any of the following 
// parameters change: peak, volume, pan, rec arm, mute, solo 
// It seems NIHIA caches certain values to save bandwidth, but this can lead to inconsistencies if we do not constantly push new data to NIHIA
// This would also explain the behaviour of the peak indicators...


class NiMidiSurface: public BaseSurface {
	public:
	NiMidiSurface()
	: BaseSurface() {
		g_connectedState = KK_NOT_CONNECTED;
	}

	virtual ~NiMidiSurface() {
		for (int i = 0; i < 8; ++i) {
			this->_sendSysex(CMD_TRACK_AVAIL, 0, i);
		}
		this->_sendCc(CMD_GOODBYE, 0);
		this->_protocolVersion = 0;
		g_connectedState = KK_NOT_CONNECTED;
	}

	virtual const char* GetTypeString() override {
		return "KompleteKontrolNiMidi";
	}

	virtual const char* GetDescString() override {
		return "Komplete Kontrol S-series Mk2/A-series/M-series";
	}

	virtual void Run() override {
		static int scanTimer = SCAN_T - 1; // first scan shall happen immediately
		static int connectCount = 0;
		static int inDev = -1;
		static int outDev = -1;
				
		static bool lightOn = false;
		static int flashTimer = -1; // EXT_EDIT_OFF: flashTimer = -1, EXT_EDIT_ACTIONS: flashTimer = -4
		static int cycleTimer = -1;
		static int cyclePos = 0;

		if (g_connectedState == KK_NOT_CONNECTED) {
			/*----------------- Scan for KK Keyboard -----------------*/
			scanTimer += 1;
			if (scanTimer >= SCAN_T) {
#ifdef CONNECTION_DIAGNOSTICS
				log_scanAttempts += 1;
				ostringstream s;
				s << "Scan # " << log_scanAttempts << endl;
				ShowConsoleMsg(s.str().c_str());
#endif
				scanTimer = 0;
				inDev = getKkMidiInput();
				if (inDev != -1) {
					outDev = getKkMidiOutput();
					if (outDev != -1) {
						this->_midiIn = CreateMIDIInput(inDev);
						this->_midiOut = CreateMIDIOutput(outDev, false, nullptr);
						if (this->_midiOut) {
							this->_midiIn->start();
							g_connectedState = KK_MIDI_FOUND;
							scanTimer = SCAN_T;
						}
					}
				}
			}
		}
		else if (g_connectedState == KK_MIDI_FOUND) {
			/*----------------- Try to connect and initialize -----------------*/
			BaseSurface::Run();
			scanTimer += 1;
			if (scanTimer >= SCAN_T) {
#ifdef CONNECTION_DIAGNOSTICS
				log_connectAttempts += 1;
				ostringstream s;
				s << "Connect # " << log_connectAttempts << endl;
				ShowConsoleMsg(s.str().c_str());
#endif
				scanTimer = 0;
				if (connectCount < CONNECT_N) {
					connectCount += 1;
					this->_sendCc(CMD_HELLO, 2); // Protocol version 2
					// ToDo: Find out if A and M series require or only support protocol version 1 or 3.
					// S Mk2 supports versions 1-3, but version 2 is best.
					// Version 1 does not fully support SHIFT button with encoder
					// Version 3 does not light up TEMPO button
				}
				else {
					int answer = ShowMessageBox("Komplete Kontrol Keyboard detected but failed to connect. Please restart NI services (NIHostIntegrationAgent), then retry. ", "ReaKontrol", 5);
					if (this->_midiIn) {
						this->_midiIn->stop();
						delete this->_midiIn;
					}
					if (this->_midiOut) {
						delete this->_midiOut;
					}
					connectCount = 0;
					if (answer == 4) {
						g_connectedState = KK_NOT_CONNECTED; // Retry -> Re-Scan
					}
					else {
						g_connectedState = -1; // Give up, plugin not doing anything
					}
				}
			}
		}
		else if (g_connectedState == KK_NIHIA_CONNECTED) {
			/*----------------- We are successfully connected -----------------*/
			if (!g_actionListLoaded) {
				loadActionList();
				g_actionListLoaded = true;
			}
		
			if (g_extEditMode == EXT_EDIT_OFF) {
				if (flashTimer != -1) { // are we returning from one of the Extended Edit Modes?
					this->_extEditButtonUpdate();
					this->_allMixerUpdate();
					lightOn = false;
					flashTimer = -1;
					cycleTimer = -1;
					cyclePos = 0;
				}
			}
			else if (g_extEditMode == EXT_EDIT_ON) {
				// Flash all Ext Edit buttons
				flashTimer += 1;
				if (flashTimer >= FLASH_T) {
					flashTimer = 0;
					if (lightOn) {
						lightOn = false;
						this->_sendCc(CMD_NAV_TRACKS, 0);
						this->_sendCc(CMD_NAV_CLIPS, 0);
						this->_sendCc(CMD_REC, 0);
						this->_sendCc(CMD_CLEAR, 0);
						this->_sendCc(CMD_LOOP, 0);
						this->_sendCc(CMD_METRO, 0);
					}
					else {
						lightOn = true;
						this->_sendCc(CMD_NAV_TRACKS, 3);
						this->_sendCc(CMD_NAV_CLIPS, 3);
						this->_sendCc(CMD_REC, 1);
						this->_sendCc(CMD_CLEAR, 1);
						this->_sendCc(CMD_LOOP, 1);
						this->_sendCc(CMD_METRO, 1);
					}
				}
			}
			else if (g_extEditMode == EXT_EDIT_LOOP) {
				if (cycleTimer == -1) {
					this->_extEditButtonUpdate();
					this->_sendCc(CMD_NAV_TRACKS, 1);
					this->_sendCc(CMD_NAV_CLIPS, 0);
				}
				// Cycle 4D Encoder LEDs
				cycleTimer += 1;
				if (cycleTimer >= CYCLE_T) {
					cycleTimer = 0;
					cyclePos += 1;
					if (cyclePos > 3) {
						cyclePos = 0;
					}
					switch (cyclePos) { // clockwise cycling
					case 0:
						this->_sendCc(CMD_NAV_TRACKS, 1);
						this->_sendCc(CMD_NAV_CLIPS, 0);
						break;
					case 1:
						this->_sendCc(CMD_NAV_TRACKS, 0);
						this->_sendCc(CMD_NAV_CLIPS, 1);
						break;
					case 2:
						this->_sendCc(CMD_NAV_TRACKS, 2);
						this->_sendCc(CMD_NAV_CLIPS, 0);
						break;
					case 3:
						this->_sendCc(CMD_NAV_TRACKS, 0);
						this->_sendCc(CMD_NAV_CLIPS, 2);
						break;
					}
				}
				// Flash LOOP button
				flashTimer += 1;
				if (flashTimer >= FLASH_T) {
					flashTimer = 0;
					if (lightOn) {
						lightOn = false;
						this->_sendCc(CMD_LOOP, 0);
					}
					else {
						lightOn = true;
						this->_sendCc(CMD_LOOP, 1);
					}
				}
			}
			else if (g_extEditMode == EXT_EDIT_TEMPO) {
				if (cycleTimer == -1) {
					this->_extEditButtonUpdate();
					this->_sendCc(CMD_NAV_TRACKS, 1);
					this->_sendCc(CMD_NAV_CLIPS, 0);
				}
				// Cycle 4D Encoder LEDs
				cycleTimer += 1;
				if (cycleTimer >= CYCLE_T) {
					cycleTimer = 0;
					cyclePos += 1;
					if (cyclePos > 3) {
						cyclePos = 0;
					}
					switch (cyclePos) { // counter clockwise cycling
					case 0:
						this->_sendCc(CMD_NAV_TRACKS, 1);
						this->_sendCc(CMD_NAV_CLIPS, 0);
						break;
					case 1:
						this->_sendCc(CMD_NAV_TRACKS, 0);
						this->_sendCc(CMD_NAV_CLIPS, 2);
						break;
					case 2:
						this->_sendCc(CMD_NAV_TRACKS, 2);
						this->_sendCc(CMD_NAV_CLIPS, 0);
						break;
					case 3:
						this->_sendCc(CMD_NAV_TRACKS, 0);
						this->_sendCc(CMD_NAV_CLIPS, 1);
						break;
					}
				}
				// Flash METRO button
				flashTimer += 1;
				if (flashTimer >= FLASH_T) {
					flashTimer = 0;
					if (lightOn) {
						lightOn = false;
						this->_sendCc(CMD_METRO, 0);
					}
					else {
						lightOn = true;
						this->_sendCc(CMD_METRO, 1);
					}
				}
			}
			else if (g_extEditMode == EXT_EDIT_ACTIONS) {
				if (flashTimer != -4) {
					this->_extEditButtonUpdate();
					lightOn = false;
					flashTimer = -4;
					cycleTimer = -1;
					cyclePos = 0;
				}
			}
			if (g_extEditMode != EXT_EDIT_ACTIONS) {
				this->_peakMixerUpdate();
			}
			BaseSurface::Run();
		}
	}

	virtual void SetPlayState(bool play, bool pause, bool rec) override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetPlayState " << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		// Update transport button lights
		if (rec) {
			this->_sendCc(CMD_REC, 1);
		}
		else {
			this->_sendCc(CMD_REC, 0);
		}
		if (pause) {
			this->_sendCc(CMD_PLAY, 1);
			this->_sendCc(CMD_STOP, 1); // since there is no Pause button on KK we indicate it with both Play and Stop lit
		}
		else if (play) {
			this->_sendCc(CMD_PLAY, 1);
			this->_sendCc(CMD_STOP, 0);
		}
		else {
			this->_sendCc(CMD_PLAY, 0);
			this->_sendCc(CMD_STOP, 1);
			if (g_KKcountInTriggered) {
				this->_disableRecCountIn(); // disable count-in for recording if it had been requested earlier by keyboard
				// Restore metronome state to last known state while COUNT IN was triggered
				if (g_KKcountInMetroState == 0) {
					Main_OnCommand(41746, 0); // Disable the metronome
				}
				else {
					Main_OnCommand(41745, 0); // Enable the metronome
				}
			}
		}
	}
		
	virtual void SetRepeatState(bool rep) override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetRepeatState " << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		// Update repeat (aka loop) button light
		if (rep) {
			this->_sendCc(CMD_LOOP, 1);
		}
		else {
			this->_sendCc(CMD_LOOP, 0);
		}
	}
	
	virtual void SetTrackListChange() override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetTrackListChange " << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		// If tracklist changes update Mixer View and ensure sanity of track and bank focus
		int numTracks = CSurf_NumTracks(false);
		// Protect against loosing track focus that could impede track navigation. Set focus on last track in this case.
		if (g_trackInFocus > numTracks) {
			g_trackInFocus = numTracks;
			// Unfortunately we cannot afford to explicitly select the last track automatically because this could screw up
			// running actions or macros. The plugin must not manipulate track selection without the user deliberately triggering
			// track selection/navigation on the keyboard (or from within Reaper).
		}
		// Protect against loosing bank focus. Set focus on last bank in this case.
		if (this->_bankStart > numTracks) {
			int lastInLastBank = numTracks % BANK_NUM_TRACKS;
			this->_bankStart = numTracks - lastInLastBank;
		}
		// If no track is selected at all (e.g. if previously selected track got removed), then this will now also show up in the
		// Mixer View. However, KK instance focus may still be present! This can be a little bit confusing for the user as typically
		// the track holding the focused KK instance will also be selected. This situation gets resolved as soon as any form of
		// track navigation/selection happens (from keyboard or from within Reaper).
		this->_allMixerUpdate();
		// ToDo: Consider sending some updates to force NIHIA to really fully update the display. Maybe in conjunction with changes to peakMixerUpdate?
		this->_metronomeUpdate(); // check if metronome status has changed on project tab change
	}

	// ToDo: If a track has Solo_Defeat activated the lights in the Mixer are not always updated correctly when navigating

	virtual void SetSurfaceSelected(MediaTrack* track, bool selected) override {
		// Use this callback for:
		// - changed track selection and KK instance focus
		// - changed automation mode
		// - changed track name

		// Using SetSurfaceSelected() rather than OnTrackSelection() or SetTrackTitle():
		// SetSurfaceSelected() is less economical because it will be called multiple times when something changes (also for unselecting tracks, change of any record arm, change of any auto mode, change of name, ...).
		// However, SetSurfaceSelected() is the more robust choice because of: https://forum.cockos.com/showpost.php?p=2138446&postcount=15
		// A good solution for efficiency is to only evaluate messages with (selected == true).
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetSurfaceSelected - Track: " << CSurf_TrackToID(track, false) << " Selected: " << selected << endl;
		ShowConsoleMsg(s.str().c_str());
#endif		
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		if (selected) {
			int id = CSurf_TrackToID(track, false);
			int numInBank = id % BANK_NUM_TRACKS;
			// ---------------- Track Selection and Instance Focus ----------------
			if (id != g_trackInFocus) {
				// Track selection has changed
				g_trackInFocus = id;
				int oldBankStart = this->_bankStart;
				this->_bankStart = id - numInBank;
				if (this->_bankStart != oldBankStart) {
					// Update everything
					this->_allMixerUpdate(); // Note: this will also update 4D track nav LEDs, g_muteStateBank and g_soloStateBank caches
				}
				else {
					// Update 4D Encoder track navigation LEDs
					int numTracks = CSurf_NumTracks(false);
					int trackNavLights = 3; // left and right on
					if (g_trackInFocus < 2) {
						trackNavLights &= 2; // left off
					}
					if (g_trackInFocus >= numTracks) {
						trackNavLights &= 1; // right off
					}
					this->_sendCc(CMD_NAV_TRACKS, trackNavLights);
				}
				if (g_trackInFocus != 0) {
					// Mark selected track as available and update Mute and Solo Button lights
					this->_sendSysex(CMD_SEL_TRACK_AVAILABLE, 1, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
					this->_sendCc(CMD_SEL_TRACK_AVAILABLE, 1); // Needed by NIHIA v1.8.8 (KK v2.1.3)
					this->_sendSysex(CMD_TOGGLE_SEL_TRACK_MUTE, g_muteStateBank[numInBank] ? 1 : 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
					this->_sendCc(CMD_TOGGLE_SEL_TRACK_MUTE, g_muteStateBank[numInBank] ? 1 : 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
					this->_sendSysex(CMD_TOGGLE_SEL_TRACK_SOLO, g_soloStateBank[numInBank], 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
					this->_sendCc(CMD_TOGGLE_SEL_TRACK_SOLO, g_soloStateBank[numInBank]); // Needed by NIHIA v1.8.8 (KK v2.1.3)
					if (g_anySolo) {
						this->_sendSysex(CMD_SEL_TRACK_MUTED_BY_SOLO, (g_soloStateBank[numInBank] == 0) ? 1 : 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
						this->_sendCc(CMD_SEL_TRACK_MUTED_BY_SOLO, (g_soloStateBank[numInBank] == 0) ? 1 : 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
					}
					else {
						this->_sendSysex(CMD_SEL_TRACK_MUTED_BY_SOLO, 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
						this->_sendCc(CMD_SEL_TRACK_MUTED_BY_SOLO, 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
					}
				}
				else {
					// Master track not available for Mute and Solo
					this->_sendSysex(CMD_SEL_TRACK_AVAILABLE, 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
					this->_sendCc(CMD_SEL_TRACK_AVAILABLE, 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
				}
				// Let Keyboard know about changed track selection
				this->_sendSysex(CMD_TRACK_SELECTED, 1, numInBank);
				// Set KK Instance Focus
				this->_sendSysex(CMD_SET_KK_INSTANCE, 0, 0, getKkInstanceName(track));
			}
			// ------------------------- Automation Mode -----------------------
			// Update automation mode
			// AUTO = ON: touch, write, latch or latch preview
			// AUTO = OFF: trim or read
			int globalAutoMode = GetGlobalAutomationOverride();
			if ((id == g_trackInFocus) && (globalAutoMode == -1)) {
				// Check automation mode of currently focused track
				int autoMode = *(int*)GetSetMediaTrackInfo(track, "I_AUTOMODE", nullptr);
				this->_sendCc(CMD_AUTO, autoMode > 1 ? 1 : 0);
			}
			else {
				// Global Automation Override
				this->_sendCc(CMD_AUTO, globalAutoMode > 1 ? 1 : 0);
			}
			// --------------------------- Track Names --------------------------
			// Update selected track name
			// Note: Rather than using a callback SetTrackTitle(MediaTrack *track, const char *title) we update the name within
			// SetSurfaceSelected as it will be called anyway when the track name changes and SetTrackTitle sometimes receives 
			// cascades of calls for all tracks even if only one name changed
			if ((id > 0) && (id >= this->_bankStart) && (id <= this->_bankEnd)) {
				char* name = (char*)GetSetMediaTrackInfo(track, "P_NAME", nullptr);
				if ((!name) || (*name == '\0')) {
					std::string s = "TRACK " + std::to_string(id);
					std::vector<char> nameGeneric(s.begin(), s.end()); // memory safe conversion to C style char
					nameGeneric.push_back('\0');
					name = &nameGeneric[0];
				}
				this->_sendSysex(CMD_TRACK_NAME, 0, numInBank, name);
			}
		}
	}
	
	// ToDo: Consider also caching volume (MIDI value and string) to improve efficiency
	virtual void SetSurfaceVolume(MediaTrack* track, double volume) override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetSurfaceVolume - Track: " << CSurf_TrackToID(track, false) << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		int id = CSurf_TrackToID(track, false);
		if ((id >= this->_bankStart) && (id <= this->_bankEnd)) {
			int numInBank = id % BANK_NUM_TRACKS;
			char volText[64] = { 0 };
			mkvolstr(volText, volume);
			this->_sendSysex(CMD_TRACK_VOLUME_TEXT, 0, numInBank, volText);
			this->_sendCc((CMD_KNOB_VOLUME0 + numInBank), volToChar_KkMk2(volume * 1.05925));
		}
	}

	// ToDo: Consider also caching pan (MIDI value and string) to improve efficiency
	virtual void SetSurfacePan(MediaTrack* track, double pan) override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetSurfacePan - Track: " << CSurf_TrackToID(track, false) << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		int id = CSurf_TrackToID(track, false);
		if ((id >= this->_bankStart) && (id <= this->_bankEnd)) {
			int numInBank = id % BANK_NUM_TRACKS;
			char panText[64];
			mkpanstr(panText, pan);
			this->_sendSysex(CMD_TRACK_PAN_TEXT, 0, numInBank, panText); // NIHIA v1.8.7.135 uses internal text
			this->_sendCc((CMD_KNOB_PAN0 + numInBank), panToChar(pan));
		}
	}
		
	virtual void SetSurfaceMute(MediaTrack* track, bool mute) override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetSurfaceMute - Track: " << CSurf_TrackToID(track, false) << " Mute " << mute << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		int id = CSurf_TrackToID(track, false);
		if (id == g_trackInFocus) {
			this->_sendSysex(CMD_TOGGLE_SEL_TRACK_MUTE, mute ? 1 : 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
			this->_sendCc(CMD_TOGGLE_SEL_TRACK_MUTE, mute ? 1 : 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
		}
		if ((id >= this->_bankStart) && (id <= this->_bankEnd)) {
			int numInBank = id % BANK_NUM_TRACKS;
			if (g_muteStateBank[numInBank] != mute) { // Efficiency: only send updates if soemthing changed
				g_muteStateBank[numInBank] = mute;
				this->_sendSysex(CMD_TRACK_MUTED, mute ? 1 : 0, numInBank);
			}
		}
	}

	// ToDo: If a track has Solo_Defeat activated the lights in the Mixer are not always updated correctly when navigating
		
	virtual void SetSurfaceSolo(MediaTrack* track, bool solo) override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetSurfaceSolo - Track: " << CSurf_TrackToID(track, false) << " Solo "<< solo << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		// Note: Solo in Reaper can have different meanings (Solo In Place, Solo In Front and much more -> Reaper Preferences)
		int id = CSurf_TrackToID(track, false);
		
		// --------- MASTER: Ignore solo on master, id = 0 is only used as an "any track is soloed" change indicator ------------
		if (id == 0) {
			// If g_anySolo state has changed update the tracks' muted by solo states within the current bank
			if (g_anySolo != solo) {
				g_anySolo = solo;
				this->_allMixerUpdate(); // Everything needs to be updated, not good enough to just update muted_by_solo states
			}
			// If any track is soloed the currently selected track will be muted by solo unless it is also soloed
			if (g_trackInFocus > 0) {
				if (g_anySolo) {
					MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
					if (!track) {
						return;
					}
					int soloState = *(int*)GetSetMediaTrackInfo(track, "I_SOLO", nullptr);
					this->_sendSysex(CMD_SEL_TRACK_MUTED_BY_SOLO, (soloState == 0) ? 1 : 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
					this->_sendCc(CMD_SEL_TRACK_MUTED_BY_SOLO, (soloState == 0) ? 1 : 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
				}
				else {
					this->_sendSysex(CMD_SEL_TRACK_MUTED_BY_SOLO, 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
					this->_sendCc(CMD_SEL_TRACK_MUTED_BY_SOLO, 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
				}
			}
			return;
		}

		// ------------------------- TRACKS: Solo state has changed on individual tracks ----------------------------------------
		if (id == g_trackInFocus) {
			this->_sendSysex(CMD_TOGGLE_SEL_TRACK_SOLO, solo ? 1 : 0, 0); // Needed by NIHIA v1.8.7 (KK v2.1.2)
			this->_sendCc(CMD_TOGGLE_SEL_TRACK_SOLO, solo ? 1 : 0); // Needed by NIHIA v1.8.8 (KK v2.1.3)
		}
		if ((id >= this->_bankStart) && (id <= this->_bankEnd)) {
			int numInBank = id % BANK_NUM_TRACKS;
			if (solo) {
				if (g_soloStateBank[numInBank] != 1) {
					g_soloStateBank[numInBank] = 1;
					this->_sendSysex(CMD_TRACK_SOLOED, 1, numInBank);
					this->_sendSysex(CMD_TRACK_MUTED_BY_SOLO, 0, numInBank);
				}
			}
			else {
				if (g_soloStateBank[numInBank] != 0) {
					g_soloStateBank[numInBank] = 0;
					this->_sendSysex(CMD_TRACK_SOLOED, 0, numInBank);
					this->_sendSysex(CMD_TRACK_MUTED_BY_SOLO, g_anySolo ? 1 : 0, numInBank);
				}
			}
		}
	}

	virtual void SetSurfaceRecArm(MediaTrack* track, bool armed) override {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: SetSurfaceRecArm - Track: " << CSurf_TrackToID(track, false) << " Armed " << armed << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (g_connectedState != KK_NIHIA_CONNECTED) return;
		// Note: record arm also leads to a cascade of other callbacks (-> filtering required!)
		int id = CSurf_TrackToID(track, false);
		if ((id >= this->_bankStart) && (id <= this->_bankEnd)) {
			int numInBank = id % BANK_NUM_TRACKS;
			this->_sendSysex(CMD_TRACK_ARMED, armed ? 1 : 0, numInBank);
		}
	}

	virtual int Extended(int call, void *parm1, void *parm2, void *parm3) override {
		if (g_connectedState != KK_NIHIA_CONNECTED) return 0;
		if (call != CSURF_EXT_SETMETRONOME) {
			return 0; // we are only interested in the metronome. Note: This works fine but does not update the status when changing project tabs
		}
		this->_sendCc(CMD_METRO, (parm1 == 0) ? 0 : 1);
		return 1;
	}

	//===============================================================================================================================

	protected:
	void _onMidiEvent(MIDI_event_t* event) override {
		if (event->midi_message[0] != MIDI_CC) {
			return;
		}
		unsigned char& command = event->midi_message[1];
		unsigned char& value = event->midi_message[2];
		
#ifdef DEBUG_DIAGNOSTICS
		ostringstream s;
		s << "Diagnostic: MIDI " << showbase << hex
			<< (int)event->midi_message[0] << " "
			<< (int)event->midi_message[1] << " "
			<< (int)event->midi_message[2] << " Focus Track "
			<< g_trackInFocus << " Bank Start "
			<< this->_bankStart << " Bank End "
			<< this->_bankEnd << " anySolo "
			<< g_anySolo << " extEditMode "
			<< g_extEditMode << " connectedState"
			<< g_connectedState << " proto version"
			<< this->_protocolVersion <<
			endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		if (command == CMD_HELLO) {
			this->_protocolVersion = value;
			if (value > 0) {
				this->_sendCc(CMD_UNDO, 1);
				this->_sendCc(CMD_REDO, 1);
				this->_sendCc(CMD_CLEAR, 1);
				this->_sendCc(CMD_QUANTIZE, 1);
				this->_allMixerUpdate();
				g_connectedState = KK_NIHIA_CONNECTED; // HELLO acknowledged = fully connected to keyboard
				Help_Set("ReaKontrol: KK-Keyboard connected", false);
#ifdef CONNECTION_DIAGNOSTICS
				ShowMessageBox("Komplete Kontrol Keyboard connected", "ReaKontrol", 0);
#endif
				return;
			}
		}

		if (g_connectedState == KK_NIHIA_CONNECTED) {
			if (g_extEditMode == EXT_EDIT_OFF) {
				// Normal Keyboard Mode
				switch (command) {
				case CMD_PLAY:
					// Toggles between play and pause
					CSurf_OnPlay();
					break;
				case CMD_RESTART:
					CSurf_GoStart();
					if (GetPlayState() & ~1) {
						// Only play if current state is not playing
						// ToDo: also need to check if recording! Because otherwise we can end up playing from start while recording elsewhere on timeline!
						CSurf_OnPlay();
					}
					break;
				case CMD_REC:
					CSurf_OnRecord();
					break;
				case CMD_COUNT:
					g_KKcountInTriggered = true;
					g_KKcountInMetroState = this->_metronomeState();
					Main_OnCommand(41745, 0); // Enable the metronome
					this->_enableRecCountIn(); // Enable count-in for recording
					CSurf_OnRecord();
					break;
				case CMD_STOP:
					CSurf_OnStop();
					break;
				case CMD_CLEAR:
					// Delete active takes. Typically, when recording an item in loop mode this allows to remove take by take until the entire item is removed.
					Main_OnCommand(40129, 0); // Edit: Delete active take (leaves empty lane if other takes present in item)
					Main_OnCommand(41349, 0); // Edit: Remove the empty take lane before the active take
					break;
				case CMD_LOOP:
					Main_OnCommand(1068, 0); // Transport: Toggle repeat
					break;
				case CMD_METRO:
					Main_OnCommand(40364, 0); // Options: Toggle metronome
					if (g_KKcountInTriggered) {
						g_KKcountInMetroState = this->_metronomeState();
					}
					break;
				case CMD_TEMPO:
					Main_OnCommand(1134, 0); // Transport: Tap tempo
					break;
				case CMD_UNDO:
					Main_OnCommand(40029, 0); // Edit: Undo
					break;
				case CMD_REDO:
					Main_OnCommand(40030, 0); // Edit: Redo
					break;
				case CMD_QUANTIZE:
					Main_OnCommand(42033, 0); // Toggle input quantize for selected track
					Main_OnCommand(40604, 0); // Open window showing track record settings
					// ToDo: Can we close the windows by e.g. SetCursorContext()?
					// ToDo: Consider indicating quantize state on keyboard by flashing button light. However, polling not CPU efficient...
					break;
				case CMD_AUTO:
					this->_onSelAutoToggle();
					break;
				case CMD_NAV_TRACKS:
					// Value is -1 or 1.
					this->_onTrackNav(convertSignedMidiValue(value));
					break;
				case CMD_NAV_BANKS:
					// Value is -1 or 1.
					this->_onBankSelect(convertSignedMidiValue(value));
					break;
				case CMD_NAV_CLIPS:
					// ToDo: Consider to also update the 4D encoder LEDs depending on marker presence and playhead position
					// Value is -1 or 1.
					Main_OnCommand(value == 1 ?
						40173 : // Markers: Go to next marker/project end
						40172, // Markers: Go to previous marker/project start
						0);
					break;
				case CMD_MOVE_TRANSPORT:
					if (value <= 63) {
						Main_OnCommand(40647, 0); // move cursor right 1 grid division (no seek)
					}
					else {
						Main_OnCommand(40646, 0); // move cursor left 1 grid division (no seek)
					}
					break;
				case CMD_MOVE_LOOP:
					if (value <= 63) {
						Main_OnCommand(40038, 0); // Shift time selection right (by its own length)
					}
					else {
						Main_OnCommand(40037, 0); // Shift time selection left (by its own length)
					}
					break;
				case CMD_TRACK_SELECTED:
					// Select a track from current bank in Mixer Mode with top row buttons
					this->_onTrackSelect(value);
					break;
				case CMD_TRACK_MUTED:
					// Toggle mute for a a track from current bank in Mixer Mode with top row buttons
					this->_onTrackMute(value);
					break;
				case CMD_TRACK_SOLOED:
					// Toggle solo for a a track from current bank in Mixer Mode with top row buttons
					this->_onTrackSolo(value);
					break;
				case CMD_KNOB_VOLUME0:
				case CMD_KNOB_VOLUME1:
				case CMD_KNOB_VOLUME2:
				case CMD_KNOB_VOLUME3:
				case CMD_KNOB_VOLUME4:
				case CMD_KNOB_VOLUME5:
				case CMD_KNOB_VOLUME6:
				case CMD_KNOB_VOLUME7:
					this->_onKnobVolumeChange(command, convertSignedMidiValue(value));
					break;
				case CMD_KNOB_PAN0:
				case CMD_KNOB_PAN1:
				case CMD_KNOB_PAN2:
				case CMD_KNOB_PAN3:
				case CMD_KNOB_PAN4:
				case CMD_KNOB_PAN5:
				case CMD_KNOB_PAN6:
				case CMD_KNOB_PAN7:
					this->_onKnobPanChange(command, convertSignedMidiValue(value));
					break;
				case CMD_PLAY_CLIP:
					// We use this for a different purpose: switch Mixer view to the bank containing the currently focused (= selected) track
					_onRefocusBank();
					break;
				case CMD_STOP_CLIP:
					// Enter Extended Edit Mode
					g_extEditMode = EXT_EDIT_ON;
					break;
				case CMD_CHANGE_SEL_TRACK_VOLUME:
					this->_onSelTrackVolumeChange(convertSignedMidiValue(value));
					break;
				case CMD_CHANGE_SEL_TRACK_PAN:
					this->_onSelTrackPanChange(convertSignedMidiValue(value));
					break;
				case CMD_TOGGLE_SEL_TRACK_MUTE:
					this->_onSelTrackMute();
					break;
				case CMD_TOGGLE_SEL_TRACK_SOLO:
					this->_onSelTrackSolo();
					break;
				default:
#ifdef BASIC_DIAGNOSTICS
					ostringstream s;
					s << "Unhandled MIDI message " << showbase << hex
						<< (int)event->midi_message[0] << " "
						<< (int)event->midi_message[1] << " "
						<< (int)event->midi_message[2] << endl;
					ShowConsoleMsg(s.str().c_str());
#endif
					break;
				}
			}
			else {
				// ==================================================================================================================	
				// Extended Edit Mode: We duplicate all commands here from Normal Mode to have finer control over behavior
				// ==================================================================================================================	
				switch (command) {

					// EXTENDED EDIT COMMANDS =======================================================================================
				case CMD_REC:
					if (g_extEditMode == EXT_EDIT_ON) {
						// ExtEdit: Toggle record arm for selected track
						Main_OnCommand(9, 0); // Toggle record arm for selected track
					}
					else {
						CSurf_OnRecord();
					}
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_CLEAR:
					if (g_extEditMode == EXT_EDIT_ON) {
						// ExtEdit: Remove Selected Track
						Main_OnCommand(40005, 0); // Remove Selected Track
						SetTrackListChange();
					}
					else {
						// Delete active takes. Typically, when recording an item in loop mode this allows to remove take by take until the entire item is removed.
						Main_OnCommand(40129, 0); // Edit: Delete active take (leaves empty lane if other takes present in item)
						Main_OnCommand(41349, 0); // Edit: Remove the empty take lane before the active take
					}
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_LOOP:
					// ToDo: ExtEdit: Change right edge of time selection +/- 1 beat length: +(#40631, #40841, #40626), -(#40631, #40842, #40626)
					if (g_extEditMode == EXT_EDIT_ON) {
						g_extEditMode = EXT_EDIT_LOOP;
					}
					else if (g_extEditMode == EXT_EDIT_LOOP) {
						g_extEditMode = EXT_EDIT_OFF;
					}
					else {
						Main_OnCommand(1068, 0); // Transport: Toggle repeat
						g_extEditMode = EXT_EDIT_OFF;
					}
					break;
				case CMD_METRO:
					if (g_extEditMode == EXT_EDIT_ON) {
						g_extEditMode = EXT_EDIT_TEMPO;
						this->_showTempoInMixer();
					}
					else if (g_extEditMode == EXT_EDIT_TEMPO) {
						this->_allMixerUpdate();
						g_extEditMode = EXT_EDIT_OFF;
					}
					else {
						Main_OnCommand(40364, 0); // Options: Toggle metronome
						if (g_KKcountInTriggered) {
							g_KKcountInMetroState = this->_metronomeState();
						}
						g_extEditMode = EXT_EDIT_OFF;
					}
					break;
				case CMD_PLAY_CLIP:
					if (g_extEditMode == EXT_EDIT_ON) {
						g_extEditMode = EXT_EDIT_ACTIONS;
						this->_showActionList(); // ExtEdit: Global Action List
					}
					else if (g_extEditMode == EXT_EDIT_ACTIONS) {
						this->_allMixerUpdate();
						this->_peakMixerUpdate();
						g_extEditMode = EXT_EDIT_OFF;
					}
					else {
						_onRefocusBank();
						g_extEditMode = EXT_EDIT_OFF;
					}
					break;
				case CMD_STOP_CLIP:
					// Exit Extended Edit Mode
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_MOVE_TRANSPORT:
				case CMD_CHANGE_SEL_TRACK_VOLUME:
				case CMD_CHANGE_SEL_TRACK_PAN:
					if (g_extEditMode == EXT_EDIT_LOOP) {
						double initCursorPos = GetCursorPosition();
						double startLoop;
						double endLoop;
						GetSet_LoopTimeRange(false, true, &startLoop, &endLoop, false); // get looping section start and end points
						SetEditCurPos(endLoop, false, false);
						if (value <= 63) {
							Main_OnCommand(40841, 0); // Move edit cursor forward 1 beat (no seek)
						}
						else {
							Main_OnCommand(40842, 0); // Move edit cursor back 1 beat (no seek)
						}
						endLoop = GetCursorPosition();
						GetSet_LoopTimeRange(true, true, &startLoop, &endLoop, false); // set looping section start and end points
						SetEditCurPos(initCursorPos, false, false);
					}
					else if (g_extEditMode == EXT_EDIT_TEMPO) {
						if (value <= 63) {
							Main_OnCommand(41129, 0); // Increase project tempo by 1bpm
						}
						else {
							Main_OnCommand(41130, 0); // Decrease project tempo by 1bpm
						}
						this->_showTempoInMixer();
					}
					break;

					// Copied Commands fron Normal Mode ================================================================================

				case CMD_PLAY:
					// Toggles between play and pause
					CSurf_OnPlay();
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_RESTART:
					CSurf_GoStart();
					if (GetPlayState() & ~1) {
						// Only play if current state is not playing
						// ToDo: also need to check if recording! Because otherwise we can end up playing from start while recording elsewhere on timeline!
						CSurf_OnPlay();
					}
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_COUNT:
					g_KKcountInTriggered = true;
					g_KKcountInMetroState = this->_metronomeState();
					Main_OnCommand(41745, 0); // Enable the metronome
					this->_enableRecCountIn(); // Enable count-in for recording
					CSurf_OnRecord();
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_STOP:
					CSurf_OnStop();
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_TEMPO:
					Main_OnCommand(1134, 0); // Transport: Tap tempo
					break;
					// ToDo: Consider also exiting Extended Edit
				case CMD_UNDO:
					Main_OnCommand(40029, 0); // Edit: Undo
					// ToDo: Consider also exiting Extended Edit
					break;
				case CMD_REDO:
					Main_OnCommand(40030, 0); // Edit: Redo
					// ToDo: Consider also exiting Extended Edit
					break;
				case CMD_QUANTIZE:
					Main_OnCommand(42033, 0); // Toggle input quantize for selected track
					Main_OnCommand(40604, 0); // Open window showing track record settings
					// ToDo: Can we close the windows by e.g. SetCursorContext()?
					// ToDo: Consider indicating quantize state on keyboard by flashing button light. However, polling not CPU efficient...
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_AUTO:
					this->_onSelAutoToggle();
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_NAV_TRACKS:
					// only navigate tracks when not in EXT_EDIT_ACTIONS mode
					if (g_extEditMode != EXT_EDIT_ACTIONS) {
						this->_onTrackNav(convertSignedMidiValue(value));
					}
					break;
				case CMD_NAV_BANKS:
					// only navigate banks when not in EXT_EDIT_ACTIONS mode
					if (g_extEditMode != EXT_EDIT_ACTIONS) {
						this->_onBankSelect(convertSignedMidiValue(value));
					}
					break;
				case CMD_NAV_CLIPS:
					// ToDo: Consider to also update the 4D encoder LEDs depending on marker presence and playhead position
					// Value is -1 or 1.
					Main_OnCommand(value == 1 ?
						40173 : // Markers: Go to next marker/project end
						40172, // Markers: Go to previous marker/project start
						0);
					break;
				case CMD_MOVE_LOOP:
					if (value <= 63) {
						Main_OnCommand(40038, 0); // Shift time selection right (by its own length)
					}
					else {
						Main_OnCommand(40037, 0); // Shift time selection left (by its own length)
					}
					break;
				case CMD_TRACK_SELECTED:
					if (g_extEditMode == EXT_EDIT_ACTIONS) {
						// Execute custom action and restore mixer display afterwards
						this->_callAction(value);
						this->_allMixerUpdate();
						this->_peakMixerUpdate();
						g_extEditMode = EXT_EDIT_OFF;
					}
					else {
						// Select a track from current bank in Mixer Mode with top row buttons
						this->_onTrackSelect(value);
					}					
					break;
				case CMD_TRACK_MUTED:
					// only when not in EXT_EDIT_ACTIONS mode: Toggle mute for a a track from current bank in Mixer Mode with top row buttons
					if (g_extEditMode != EXT_EDIT_ACTIONS) {
						this->_onTrackMute(value);
						g_extEditMode = EXT_EDIT_OFF;
					}					
					break;
				case CMD_TRACK_SOLOED:
					// only when not in EXT_EDIT_ACTIONS mode: Toggle solo for a a track from current bank in Mixer Mode with top row buttons
					if (g_extEditMode != EXT_EDIT_ACTIONS) {
						this->_onTrackSolo(value);
						g_extEditMode = EXT_EDIT_OFF;
					}					
					break;
				case CMD_KNOB_VOLUME0:
				case CMD_KNOB_VOLUME1:
				case CMD_KNOB_VOLUME2:
				case CMD_KNOB_VOLUME3:
				case CMD_KNOB_VOLUME4:
				case CMD_KNOB_VOLUME5:
				case CMD_KNOB_VOLUME6:
				case CMD_KNOB_VOLUME7:
					// only change track volume when not in EXT_EDIT_ACTIONS mode
					if (g_extEditMode != EXT_EDIT_ACTIONS) {
						this->_onKnobVolumeChange(command, convertSignedMidiValue(value));
						g_extEditMode = EXT_EDIT_OFF;					
					}					
					break;
				case CMD_KNOB_PAN0:
				case CMD_KNOB_PAN1:
				case CMD_KNOB_PAN2:
				case CMD_KNOB_PAN3:
				case CMD_KNOB_PAN4:
				case CMD_KNOB_PAN5:
				case CMD_KNOB_PAN6:
				case CMD_KNOB_PAN7:
					// only change track pan when not in EXT_EDIT_ACTIONS mode
					if (g_extEditMode != EXT_EDIT_ACTIONS) {
						this->_onKnobPanChange(command, convertSignedMidiValue(value));
						g_extEditMode = EXT_EDIT_OFF;
					}					
					break;
				case CMD_TOGGLE_SEL_TRACK_MUTE:
					this->_onSelTrackMute();
					g_extEditMode = EXT_EDIT_OFF;
					break;
				case CMD_TOGGLE_SEL_TRACK_SOLO:
					this->_onSelTrackSolo();
					g_extEditMode = EXT_EDIT_OFF;
					break;
				default:
#ifdef BASIC_DIAGNOSTICS
					ostringstream s;
					s << "Unhandled MIDI message " << showbase << hex
						<< (int)event->midi_message[0] << " "
						<< (int)event->midi_message[1] << " "
						<< (int)event->midi_message[2] << endl;
					ShowConsoleMsg(s.str().c_str());
#endif
					break;
				}
			}
		}
	}

	//===============================================================================================================================

	private:
	int _protocolVersion = 0;
	int _bankStart = 0;
	int _bankEnd = 0;

	void _peakMixerUpdate() {
		// Peak meters. Note: Reaper reports peak, NOT VU	

		// ToDo: Peak Hold in KK display shall be erased immediately when changing bank
		// ToDo: Peak Hold in KK display shall be erased after decay time t when track muted or no signal.
		// ToDo: Explore the effect of sending CMD_SEL_TRACK_PARAMS_CHANGED after sending CMD_TRACK_VU
		// ToDo: Consider caching and not sending anything via SysEx if no values have changed.

		// Meter information is sent to KK as array (string of chars) for all 16 channels (8 x stereo) of one bank.
		// A value of 0 will result in stopping to refresh meters further to right as it is interpretated as "end of string".
		// peakBank[0]..peakBank[31] are used for data. The array needs one additional last char peakBank[32] set as "end of string" marker.
		static char peakBank[(BANK_NUM_TRACKS * 2) + 1];
		int j = 0;
		double peakValue = 0;
		int numInBank = 0;
		for (int id = this->_bankStart; id <= this->_bankEnd; ++id, ++numInBank) {
			MediaTrack* track = CSurf_TrackFromID(id, false);
			if (!track) {
				break;
			}
			j = 2 * numInBank;
			if (HIDE_MUTED_BY_SOLO) {
				// If any track is soloed then only soloed tracks and the master show peaks (irrespective of their mute state)
				if (g_anySolo) {
					if ((g_soloStateBank[numInBank] == 0) && (((numInBank != 0) && (this->_bankStart == 0)) || (this->_bankStart != 0))) {
						peakBank[j] = 1;
						peakBank[j + 1] = 1;
					}
					else {
						peakValue = Track_GetPeakInfo(track, 0); // left channel
						peakBank[j] = volToChar_KkMk2(peakValue); // returns value between 1 and 127
						peakValue = Track_GetPeakInfo(track, 1); // right channel
						peakBank[j + 1] = volToChar_KkMk2(peakValue); // returns value between 1 and 127
					}
				}
				// If no tracks are soloed then muted tracks shall show no peaks
				else {
					if (g_muteStateBank[numInBank]) {
						peakBank[j] = 1;
						peakBank[j + 1] = 1;
					}
					else {
						peakValue = Track_GetPeakInfo(track, 0); // left channel
						peakBank[j] = volToChar_KkMk2(peakValue); // returns value between 1 and 127
						peakValue = Track_GetPeakInfo(track, 1); // right channel
						peakBank[j + 1] = volToChar_KkMk2(peakValue); // returns value between 1 and 127					
					}
				}
			}
			else {
				// Muted tracks that are NOT soloed shall show no peaks. Tracks muted by solo show peaks but they appear greyed out.
				if ((g_soloStateBank[numInBank] == 0) && (g_muteStateBank[numInBank])) {
					peakBank[j] = 1;
					peakBank[j + 1] = 1;
				}
				else {
					peakValue = Track_GetPeakInfo(track, 0); // left channel
					peakBank[j] = volToChar_KkMk2(peakValue); // returns value between 1 and 127
					peakValue = Track_GetPeakInfo(track, 1); // right channel
					peakBank[j + 1] = volToChar_KkMk2(peakValue); // returns value between 1 and 127					
				}
			}
		}
		peakBank[j + 2] = '\0'; // end of string (no tracks available further to the right)
		this->_sendSysex(CMD_TRACK_VU, 2, 0, peakBank);
	}

	void _allMixerUpdate() {
#ifdef CALLBACK_DIAGNOSTICS
		ostringstream s;
		s << "CALL: allMixerUpdate " << endl;
		ShowConsoleMsg(s.str().c_str());
#endif
		int numInBank = 0;
		this->_bankEnd = this->_bankStart + BANK_NUM_TRACKS - 1; // avoid ambiguity: track counting always zero based
		int numTracks = CSurf_NumTracks(false); 
		// Update bank select button lights
		// ToDo: Consider optimizing this piece of code
		int bankLights = 3; // left and right on
		if (numTracks < BANK_NUM_TRACKS) {
			bankLights = 0; // left and right off
		}
		else if (this->_bankStart == 0) {
			bankLights = 2; // left off, right on
		}
		else if (this->_bankEnd >= numTracks) {
			bankLights = 1; // left on, right off
		}
		this->_sendCc(CMD_NAV_BANKS, bankLights);
		if (this->_bankEnd > numTracks) {
			this->_bankEnd = numTracks;
			// Mark additional bank tracks as not available
			int lastInLastBank = numTracks % BANK_NUM_TRACKS;
			for (int i = 7; i > lastInLastBank; --i) {
				this->_sendSysex(CMD_TRACK_AVAIL, 0, i);				
			}
		}
		// Update 4D Encoder track navigation LEDs
		int trackNavLights = 3; // left and right on
		if (g_trackInFocus < 2) {
			trackNavLights &= 2; // left off
		}
		if (g_trackInFocus >= numTracks) {
			trackNavLights &= 1; // right off
		}
		this->_sendCc(CMD_NAV_TRACKS, trackNavLights);
		// Update current bank
		for (int id = this->_bankStart; id <= this->_bankEnd; ++id, ++numInBank) {
			MediaTrack* track = CSurf_TrackFromID(id, false);
			if (!track) {
				break;
			}			
			// Master track needs special consideration: no soloing, no record arm
			if (id == 0) {
				this->_sendSysex(CMD_TRACK_AVAIL, TRTYPE_MASTER, 0);
				this->_sendSysex(CMD_TRACK_NAME, 0, 0, "MASTER");
				this->_sendSysex(CMD_TRACK_SOLOED, 0, 0);
				this->_sendSysex(CMD_TRACK_MUTED_BY_SOLO, 0, 0);
				this->_sendSysex(CMD_TRACK_ARMED, 0, 0);
			}
			// Ordinary tracks can be soloed and record armed
			else {
				this->_sendSysex(CMD_TRACK_AVAIL, TRTYPE_UNSPEC, numInBank);
				int soloState = *(int*)GetSetMediaTrackInfo(track, "I_SOLO", nullptr);
				if (soloState == 0) {
					g_soloStateBank[numInBank] = 0;
					this->_sendSysex(CMD_TRACK_SOLOED, 0, numInBank);
					this->_sendSysex(CMD_TRACK_MUTED_BY_SOLO, g_anySolo ? 1 : 0, numInBank);
				}
				else {
					g_soloStateBank[numInBank] = 1;
					this->_sendSysex(CMD_TRACK_SOLOED, 1, numInBank);
					this->_sendSysex(CMD_TRACK_MUTED_BY_SOLO, 0, numInBank);
				}
				int armed = *(int*)GetSetMediaTrackInfo(track, "I_RECARM", nullptr);
				this->_sendSysex(CMD_TRACK_ARMED, armed, numInBank);
				char* name = (char*)GetSetMediaTrackInfo(track, "P_NAME", nullptr);
				if ((!name) || (*name == '\0')) {
					std::string s = "TRACK " + std::to_string(id);
					std::vector<char> nameGeneric(s.begin(), s.end()); // memory safe conversion to C style char
					nameGeneric.push_back('\0');
					name = &nameGeneric[0];
				}
				this->_sendSysex(CMD_TRACK_NAME, 0, numInBank, name);
			}			
			this->_sendSysex(CMD_TRACK_SELECTED, id == g_trackInFocus ? 1 : 0, numInBank);
			bool muted = *(bool*)GetSetMediaTrackInfo(track, "B_MUTE", nullptr);
			g_muteStateBank[numInBank] = muted;
			this->_sendSysex(CMD_TRACK_MUTED, muted ? 1 : 0, numInBank);
			double volume = *(double*)GetSetMediaTrackInfo(track, "D_VOL", nullptr);
			char volText[64];
			mkvolstr(volText, volume);
			this->_sendSysex(CMD_TRACK_VOLUME_TEXT, 0, numInBank, volText);
			this->_sendCc((CMD_KNOB_VOLUME0 + numInBank), volToChar_KkMk2(volume * 1.05925));			
			double pan = *(double*)GetSetMediaTrackInfo(track, "D_PAN", nullptr);
			char panText[64];	
			mkpanstr(panText, pan);
			this->_sendSysex(CMD_TRACK_PAN_TEXT, 0, numInBank, panText); // NIHIA v1.8.7.135 uses internal text
			this->_sendCc((CMD_KNOB_PAN0 + numInBank), panToChar(pan));
		}
	}

	void _showActionList() {
		static char clearPeak[(BANK_NUM_TRACKS * 2) + 1] = { 2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,0 };
		this->_sendCc(CMD_NAV_BANKS, 0);
		for (int i = 0; i <= 7; ++i) {
			if (g_actionList.ID[i] > 0) {
				this->_sendSysex(CMD_TRACK_AVAIL, TRTYPE_MIDI, i);
				this->_sendSysex(CMD_TRACK_SOLOED, 1, i);
				this->_sendSysex(CMD_TRACK_MUTED_BY_SOLO, 0, i);
				this->_sendSysex(CMD_TRACK_MUTED, 0, i);
				this->_sendSysex(CMD_TRACK_ARMED, 0, i);
				this->_sendSysex(CMD_TRACK_SELECTED, 0, i);
				if (g_actionList.name[i][0] == '\0') {
					this->_sendSysex(CMD_TRACK_NAME, 0, i, "NAME?");
				}
				else {
					this->_sendSysex(CMD_TRACK_NAME, 0, i, &g_actionList.name[i][0]);
				}
				this->_sendSysex(CMD_TRACK_VOLUME_TEXT, 0, i, "Action");
				this->_sendSysex(CMD_TRACK_PAN_TEXT, 0, i, "Action");
				this->_sendCc((CMD_KNOB_VOLUME0 + i), 1);
				this->_sendCc((CMD_KNOB_PAN0 + i), 63);
			}
			else {
				this->_sendSysex(CMD_TRACK_AVAIL, 0, i);
				this->_sendSysex(CMD_TRACK_SOLOED, 0, i);
				this->_sendSysex(CMD_TRACK_MUTED_BY_SOLO, 0, i);
				this->_sendSysex(CMD_TRACK_MUTED, 0, i);
				this->_sendSysex(CMD_TRACK_ARMED, 0, i);
				this->_sendSysex(CMD_TRACK_SELECTED, 0, i);
				this->_sendSysex(CMD_TRACK_NAME, 0, i, "");
				this->_sendSysex(CMD_TRACK_VOLUME_TEXT, 0, i, " ");
				this->_sendSysex(CMD_TRACK_PAN_TEXT, 0, i, " ");
				this->_sendCc((CMD_KNOB_VOLUME0 + i), 1);
				this->_sendCc((CMD_KNOB_PAN0 + i), 63);
			}
		}
		if (g_actionList.ID[0] == -1) {
			this->_sendSysex(CMD_TRACK_AVAIL, TRTYPE_MIDI, 0);
			this->_sendSysex(CMD_TRACK_NAME, 0, 0, "Config file not found!");
		}
		this->_sendSysex(CMD_TRACK_VU, 2, 0, clearPeak);
	}

	void _callAction(unsigned char actionSlot) {
		if (g_actionList.ID[actionSlot]) {
			Main_OnCommand(g_actionList.ID[actionSlot], 0);
			// SetTrackListChange(); // TBD is this necessary? or call SetSurfaceSelected or whatever may have changed?
		}		
	}

	void _extEditButtonUpdate() {
		this->_sendCc(CMD_CLEAR, 1);
		this->_metronomeUpdate();
		if (GetPlayState() & 4) {
			this->_sendCc(CMD_REC, 1);
		}
		else {
			this->_sendCc(CMD_REC, 0);
		}
		if (GetSetRepeat(-1)) {
			this->_sendCc(CMD_LOOP, 1);
		}
		else {
			this->_sendCc(CMD_LOOP, 0);
		}
		int numTracks = CSurf_NumTracks(false);
		int trackNavLights = 3; // left and right on
		if (g_trackInFocus < 2) {
			trackNavLights &= 2; // left off
		}
		if (g_trackInFocus >= numTracks) {
			trackNavLights &= 1; // right off
		}
		this->_sendCc(CMD_NAV_TRACKS, trackNavLights);
		this->_sendCc(CMD_NAV_CLIPS, 0); // ToDo: also restore  these lights to correct values
	}

	void _showTempoInMixer() {
		// Show BPM at Reaper's current edit cursor position in KK Mixer View
		double time = GetCursorPosition();
		int timesig_numOut = 0;
		int timesig_denomOut = 0;
		double tempoOut = 0;
		TimeMap_GetTimeSigAtTime(nullptr, time, &timesig_numOut, &timesig_denomOut, &tempoOut);
		std::string s = std::to_string((int)(tempoOut + 0.5)) + " BPM";
		std::vector<char> bpmText(s.begin(), s.end()); // memory safe conversion to C style char
		bpmText.push_back('\0');
		char* name = &bpmText[0];
		this->_sendSysex(CMD_TRACK_VOLUME_TEXT, 0, 0, name);
	}

	void _clearAllSelectedTracks() {
		// Clear all selected tracks. Copyright (c) 2010 and later Tim Payne (SWS)
		int iSel = 0;
		for (int i = 0; i <= GetNumTracks(); i++) // really ALL tracks, hence no use of CSurf_NumTracks
			GetSetMediaTrackInfo(CSurf_TrackFromID(i, false), "I_SELECTED", &iSel);
	}

	void _onTrackSelect(unsigned char numInBank) {
		int id = this->_bankStart + numInBank;
		if (id > CSurf_NumTracks(false)) {
			return;
		}
		MediaTrack* track = CSurf_TrackFromID(id, false);
		int iSel = 1; // "Select"
		_clearAllSelectedTracks(); 
		GetSetMediaTrackInfo(track, "I_SELECTED", &iSel);
	}

	void _onTrackNav(signed char value) {
		// Move to next/previous track relative to currently focused track = last selected track
		int numTracks = CSurf_NumTracks(false);
		// Backstop measure to protect against unreported track removal that was not captured in SetTrackListChange callback due to race condition
		if (g_trackInFocus > numTracks) { 
			g_trackInFocus = numTracks; 
		}
		if (((g_trackInFocus <= 1) && (value < 0)) ||
			((g_trackInFocus >= numTracks) && (value >0 ))) {
			return;
		}
		int id = g_trackInFocus + value;
		MediaTrack* track = CSurf_TrackFromID(id, false);
		int iSel = 1; // "Select"
		_clearAllSelectedTracks();
		GetSetMediaTrackInfo(track, "I_SELECTED", &iSel);
	}

	void _onBankSelect(signed char value) {
		// Manually switch the bank visible in Mixer View WITHOUT influencing track selection
		int newBankStart = this->_bankStart + (value * BANK_NUM_TRACKS);
		int numTracks = CSurf_NumTracks(false); 
		if ((newBankStart < 0) || (newBankStart > numTracks)) {
			return;
		}
		this->_bankStart = newBankStart;
		this->_allMixerUpdate();
	}

	void _onRefocusBank() {
		// Switch Mixer view to the bank containing the currently focused (= selected) track and also focus Reaper's TCP and MCP
		if (g_trackInFocus < 1) {
			return;
		}
		int numTracks = CSurf_NumTracks(false);
		// Backstop measure to protect against unreported track removal that was not captured in SetTrackListChange callback due to race condition
		if (g_trackInFocus > numTracks) {
			g_trackInFocus = numTracks;
		}
		MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
		int iSel = 1; // "Select"
		_clearAllSelectedTracks();
		GetSetMediaTrackInfo(track, "I_SELECTED", &iSel);
		Main_OnCommand(40913, 0); // Vertical scroll selected track into view (TCP)
		SetMixerScroll(track); // Horizontal scroll making the selected track the leftmost track if possible (MCP)
		this->_bankStart = (int)(g_trackInFocus / BANK_NUM_TRACKS) * BANK_NUM_TRACKS;
		this->_allMixerUpdate();
	}

	void _onTrackMute(unsigned char numInBank) {
		int id = this->_bankStart + numInBank;
		if ((id == 0) || (id > CSurf_NumTracks(false))) {
			return;
		}
		MediaTrack* track = CSurf_TrackFromID(id, false);
		bool muted = *(bool*)GetSetMediaTrackInfo(track, "B_MUTE", nullptr);
		CSurf_OnMuteChange(track, muted ? 0 : 1);
	}

	void _onTrackSolo(unsigned char numInBank) {		
		int id = this->_bankStart + numInBank;
		if ((id == 0) || (id > CSurf_NumTracks(false))) {
			return;
		}
		MediaTrack* track = CSurf_TrackFromID(id, false);
		int soloState = *(int*)GetSetMediaTrackInfo(track, "I_SOLO", nullptr);
		CSurf_OnSoloChange(track, soloState == 0 ? 1 : 0); // ToDo: Consider settings solo state to 2 (soloed in place)? If at all it this behaviour should be configured with a CONST
	}

	void _onKnobVolumeChange(unsigned char command, signed char value) {
		int numInBank = command - CMD_KNOB_VOLUME0;
		double dvalue = static_cast<double>(value);
		MediaTrack* track = CSurf_TrackFromID(numInBank + this->_bankStart, false);
		if (!track) {
			return;
		}
		CSurf_SetSurfaceVolume(track, CSurf_OnVolumeChange(track, dvalue / 126.0, true), nullptr); 
		// ToDo: non linear behaviour especially in the lower volumes would be preferable. 
	}

	void _onKnobPanChange(unsigned char command, signed char value) {
		int numInBank = command - CMD_KNOB_PAN0;
		double dvalue = static_cast<double>(value);
		MediaTrack* track = CSurf_TrackFromID(numInBank + this->_bankStart, false);
		if (!track) {
			return;
		}
		CSurf_SetSurfacePan(track, CSurf_OnPanChange(track, dvalue * 0.00098425, true), nullptr); 
		// scaling by dividing by 127*8 (0.00098425)
		// ToDo: slightly change this?
	}

	void _onSelTrackVolumeChange(signed char value) {
		// Coarse scaling ( value = +/-63): exactly 1dB step per single click
		// Fine scaling (value = +/- 12): exactly 0.1dB per single click
		MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
		if (!track) {
			return;
		}
		if (value >=0 ) {
			CSurf_SetSurfaceVolume(track, CSurf_OnVolumeChange(track, value > 38 ? 1.0 : 0.1, true), nullptr);
		}
		else {
			CSurf_SetSurfaceVolume(track, CSurf_OnVolumeChange(track, value < -38 ? -1.0 : -0.1, true), nullptr);
		}
	}

	void _onSelTrackPanChange(signed char value) {
		double dvalue = static_cast<double>(value);
		MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
		if (!track) {
			return;
		}
		CSurf_SetSurfacePan(track, CSurf_OnPanChange(track, dvalue * 0.00098425, true), nullptr);
	}
	
	void _onSelTrackMute() {
		if (g_trackInFocus > 0) {
			MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
			if (!track) {
				return;
			}
			bool muted = *(bool*)GetSetMediaTrackInfo(track, "B_MUTE", nullptr);
			CSurf_OnMuteChange(track, muted ? 0 : 1);
		}		
	}
	
	void _onSelTrackSolo() {
		if (g_trackInFocus > 0) {
			MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
			if (!track) {
				return;
			}
			int soloState = *(int*)GetSetMediaTrackInfo(track, "I_SOLO", nullptr);
			CSurf_OnSoloChange(track, soloState == 0 ? 1 : 0); // ToDo: Consider settings solo state to 2 (soloed in place)? If at all it this behaviour should be configured with a CONST
		}		
	}

	void _onSelAutoToggle() {
		if (g_trackInFocus == 0) {
			// Special function: If the master track is selected & focused then the AUTO button toggles GlobalAutomationOverride
			int globalAutoMode = GetGlobalAutomationOverride();
			if (globalAutoMode > 1) {
				globalAutoMode = -1; // no global override
			}
			else {
				globalAutoMode = 4; // set global override to latch mode
			}
			SetGlobalAutomationOverride(globalAutoMode);
		}
		if (g_trackInFocus > 0) {
			// Toggle automation of individual tracks
			MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
			if (!track) {
				return;
			}
			int autoMode = *(int*)GetSetMediaTrackInfo(track, "I_AUTOMODE", nullptr);
			if (autoMode > 1) {
				autoMode = 0; // set to trim mode
			}
			else {
				autoMode = 4; // set to latch mode
			}
			GetSetMediaTrackInfo(track, "I_AUTOMODE", &autoMode);
		}
	}

	/*
	bool _isSelTrackInputQuantize() {
		MediaTrack* track = CSurf_TrackFromID(g_trackInFocus, false);
		if (!track) {
			return false;
		}
		// ToDo:
		// Note: the below code is wasteful in terms of resource usage (memory and CPU). This is ok as long as we do not
		// poll for InputQuantize state frequently, i.e. just on button push. If this changes in the future the code
		// can be improved in many ways (e.g. reducing the search to start at position (200 + [track name length]) which is the
		// approximate position in rppXML for INQ). Also avoids the misinterpretation of a track name containing "INQ "...
		char rppxml[514];
		rppxml[513] = '\0';
		int size = 512;
		GetTrackStateChunk(track, rppxml, size, false);
		// rppxml contains keyword "INQ" for InputQuantize followed by numbers separated by spaces. Encoding:
		// enabled = t[1],
		// grid = t[4],
		// positioning = t[2], -- 0 nearest - 1 previous 1 next
		// quantize_NoteOffs = t[3],
		// strength = t[5],
		// swing = t[6],
		// quantize_within1 = t[7],
		// quantize_within2 = t[8]
		char *pos = strstr(rppxml, "INQ ");
		ptrdiff_t index = pos - rppxml;
		index += 4;
		if (rppxml[index] == '1') {
			return true;
		}
		else {
			return false;
		}
	}

	void _onQuantize() {
		// Toggle MIDI Input Quantize for selected track enable / disable(#42063 / #42064)
		
		// ToDo: How to indicate quantize state on keyboard?? Maybe flash the button?
		// Polling quantize state could be done in SetSurfaceSelected() for the currently selected track
		
		// ToDo: Can we close these windows by e.g. SetCursorContext(), or smth w DockWindowRemove, DockWindowRefresh...? Could be triggered when receiving any other MIDI CC command...
		// changing cursor/mouse context could be quite annoying if triggered from GUI -> only consider for MIDI CCs...
		// If this works consider settings cursor context back to what is was, i.e. first store GetCursorContext()
		// or checkout SWS Focus main window _S&M_WINMAIN ...

		if (_isSelTrackInputQuantize()) {
			Main_OnCommand(42064, 0); // disable input quantize for selected track
			Main_OnCommand(40604, 0); // open window showing track record settings
		}
		else {
			Main_OnCommand(42063, 0); // disable input quantize for selected track
			Main_OnCommand(40604, 0); // open window showing track record settings
		}
	}
	*/

	void* GetConfigVar(const char* cVar) { // Copyright (c) 2010 and later Tim Payne (SWS), Jeffos
		int sztmp;
		void* p = NULL;
		if (int iOffset = projectconfig_var_getoffs(cVar, &sztmp))
		{
			p = projectconfig_var_addr(EnumProjects(-1, NULL, 0), iOffset);
		}
		else
		{
			p = get_config_var(cVar, &sztmp);
		}
		return p;
	}

	int _metronomeState() {
		return (*(int*)GetConfigVar("projmetroen") & 1);
	}

	void _metronomeUpdate() {
		// Actively poll the metronome status on project tab changes
		this->_sendCc(CMD_METRO, this->_metronomeState());
	}

	void _enableRecCountIn() {
		*(int*)GetConfigVar("projmetroen") |= 16; 
	}

	void _disableRecCountIn() {
		*(int*)GetConfigVar("projmetroen") &= ~16;
		g_KKcountInTriggered = false;
	}

	void _sendCc(unsigned char command, unsigned char value) {
		if (this->_midiOut) {
			this->_midiOut->Send(MIDI_CC, command, value, -1);
		}
	}

	void _sendSysex(unsigned char command, unsigned char value,
		unsigned char track, const string& info = ""
	) {
		if (!this->_midiOut) {
			return;
		}
		int length = sizeof(MIDI_SYSEX_BEGIN)
			+ 3 // command, value, track
			+ info.length()
			+ sizeof(MIDI_SYSEX_END);
		// MIDI_event_t includes 4 bytes for the message, but we need more.
		MIDI_event_t* event = (MIDI_event_t*)new unsigned char[
			sizeof(MIDI_event_t) - 4 + length];
		event->frame_offset = 0;
		event->size = length;
		memcpy(event->midi_message, MIDI_SYSEX_BEGIN,
			sizeof(MIDI_SYSEX_BEGIN));
		int messagePos = sizeof(MIDI_SYSEX_BEGIN);
		event->midi_message[messagePos++] = command;
		event->midi_message[messagePos++] = value;
		event->midi_message[messagePos++] = track;
		memcpy(event->midi_message + messagePos, info.c_str(), info.length());
		messagePos += info.length();
		event->midi_message[messagePos++] = MIDI_SYSEX_END;
		this->_midiOut->SendMsg(event, -1);
		delete event;
	}

};

IReaperControlSurface* createNiMidiSurface() {
	return new NiMidiSurface();
}
