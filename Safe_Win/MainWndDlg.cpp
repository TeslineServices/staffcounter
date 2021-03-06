**********************************************************************
** Copyright (C) 2009-2016 Tesline-Service S.R.L.  All rights reserved.
**
** StaffCounter Agent for Windows 
** 
**
** This file may be distributed and/or modified under the terms of the
** GNU General Public License version 2 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://StaffCounter.net/ for GPL licensing information.
**
** Contact info@rohos.com if any conditions of this licensing are
** not clear to you.
**
**********************************************************************/


// MainWndDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MainWnd.h"
#include "MainWndDlg.h"


#include "common1.h"
#include "psapi.h"
//#include <dbt.h>
#include <io.h>
#include <winuser.h>
#include <Windowsx.h> 
#include <atlimage.h>
#include "report.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void GetMySessionID();
bool isCurrentDesktopMy();
void WINAPI AddLOG_message_me(char *message, int len, int UTF8encode=true/*add_time*/);

void AddLOG_message2(CString str, CString attr, int type);
bool UploadLogDataNow(int flush_buffer);
void WriteAllDurations();

int idc_users_controls[9] = {IDC_CHECK1, IDC_CHECK2, IDC_CHECK9, IDC_CHECK11, IDC_CHECK14, IDC_CHECK15, IDC_CHECK16, IDC_CHECK17, 0};

char G_start_in_reg_mode;

#define WM_TRAY_MESSAGE WM_USER+100

#define WM_CLOSEAPP_UPGRADE WM_USER+107

void OnStart(BOOL reload_cfg=0);
bool RegisterMySelf(bool remove, bool allusers);

void GetMySessionID();
bool isCurrentDesktopMy();

CMainWndDlg* pMainWnd;

HWND hwndNextViewer;
TCHAR user_name[150];
DWORD total_mins_session = 0;
int current_day_int = 0;

const UINT WM_TASKBARCREATED = 
    ::RegisterWindowMessage(_T("TaskbarCreated"));

#define APP_MONITOR_TIMER_ELAPSE 1800
#define URL_UI_MAX_LEN	70


HWND curr_wnd =0; // remember last active window
WCHAR _currentWindowName[200]={0};
DWORD _session_id=0;
BOOL desktop_switch = false;
DWORD _current_pid; // current process PID
TCHAR _currentProcessName[500];

CMainWndDlg* pWnd;

typedef DWORD (__stdcall *WTSGetActiveConsoleSessionId_t)();
WTSGetActiveConsoleSessionId_t pWTSGetActiveConsoleSessionId;

typedef void (WINAPI *AddLOG_message_t)(char *,int,int);
AddLOG_message_t				AddLOG_message = AddLOG_message_me;



HWND hWnd;
DWORD timer_id=0;

TCHAR last_url[300];  // last visited URL
BOOL g_UserInputIsIdle = false; // user input is idle for 7 or more seconds.
CTime idle_time_start;
BOOL _log_app_duration; // if 1 - log how much time user working per app

BOOL _GIIS_5minis_passed = false;;

TCHAR STR_FORMAT_APP0[100] = {"<p class=\"app\" time=\"%s\" name=\"%s\">%s</p>\n"};
TCHAR STR_FORMAT_URL0[100] = {"<p class=\"url\" time=\"%s\" href=\"%s\">%s</p>\n"};

TCHAR STR_FORMAT_APP[100] = {"<p class=\"app\" time=\"%s\" dur=\"%d\" name=\"%s\">%s</p>\n"};
TCHAR STR_FORMAT_URL[100] = {"<p class=\"url\" time=\"%s\" dur=\"%d\" href=\"%s\">%s</p>\n"};

TCHAR STR_FORMAT_IDLE[100] = {"<p class=\"idle\" time=\"%s\" dur=\"%s\" >Computer was idle for: %s</p>\n"};

TCHAR STR_FORMAT_SYSTEM[50] = {"<p class=\"system\" time=\"%s\">%s</p>"};

TCHAR STR_FORMAT_TIME[50] = {"%H:%M:%S"};
TCHAR STR_FORMAT_TIME_MARK[50] = {"<p class=\"time_mark\" time=\"%H:%M\">%H:%M</p>\n "};

int g_max_work_mins_upload = (20 * 60); // each 20 mins of working - upload data.
int g_total_sec_for_period = 0;

char cr[] = {"\x0\x0"};



DWORD GetInputIdleSeconds()
{
	LASTINPUTINFO lii;
	lii.cbSize = sizeof(LASTINPUTINFO);

	GetLastInputInfo( &lii);

	DWORD time_span = GetTickCount() - lii.dwTime;

	return time_span / 1000;
}

// return TRUE - if user input is idle for 7 or more secs
// return FALSE - if there is no input idle
//  on the idle finish - add idle span to LOG
bool CMainWndDlg::IsUserInputIdle()
{
	DWORD GIIS = GetInputIdleSeconds();

	if (g_UserInputIsIdle)
	{
		if (GIIS > 180 && _log_app_duration )  // 150 sec of "user input idle"
		{
			// we track user time in App and URLs			
			AddLOG_message2("", "", 5); // lets stop active App log (this will allso stop URL). this will also call AddLOG_message() - flush log buffer			
		}
	
		if (GIIS > (4 * 60) && GIIS < 4000 ) // upload log file after 5 mins of Idle
		{
			// we track user time in App and URLs			
			if (false == _GIIS_5minis_passed)
			{
				_GIIS_5minis_passed = true;
				printf_log("5 mins of inactivity");
				UploadLogDataNow(1);	
			}
		}
		
		if ( GIIS > 10) // idle input is continue... 
		{
			return true;
		}

		/* idle time is finished now!*/

		_GIIS_5minis_passed = false;		
		g_UserInputIsIdle = false;   // input idle time is finished by user;
		CTime currtime = CTime::GetCurrentTime();
		CTimeSpan ts = currtime - idle_time_start;

		// log idle time only within a Day...
		if (ts.GetTotalSeconds() >= 1 && ts.GetTotalMinutes() < 2 * 60 ) // from 1 min up to 5 hours - means idle
		{			
			CString s1;
			s1.Format("%d", ts.GetTotalSeconds() );
			AddLOG_message2(s1, ts.Format("%H hours %M mins"), 18); // Log Idle time.

			printf_log("idle end %s sec (0x%X sec)", (LPCTSTR)s1, GIIS);			


			/* reset "App monitor"  */		
			curr_wnd = (HWND)0xFFFFFF;  
			_currentWindowName[0]=0;			
			_current_pid =0;				
			/*------------------- */
		}
	}

	if (GIIS > 180) // idle user input is started (was 30)
	{
		g_UserInputIsIdle = true;
		idle_time_start = CTime::GetCurrentTime();
		
		printf_log("Idle start %s (GIIS %d)", (LPCTSTR)idle_time_start.Format("%H:%M:%S - day: %d"), GIIS);

		if (GIIS > (60*60))
		{
			printf_log("Not true idle start ");
			g_UserInputIsIdle = false;			
		}
		return true;
	}

	return false;
}

HANDLE lastProcess = 0;

// Get LOG file name for current user
void GetFileName(int i, char* str, char* ext)
{
	SYSTEMTIME tm;
	UINT cch = 30;
	TCHAR date[250];
	char username[100];
	DWORD sz=50;
	GetLocalTime(&tm);	

	GetUserName( username, &sz);
	GetDateFormat(0x0409, 0, &tm, "d MMMM','dddd", date, cch);		


	ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "LogFilesPath", str, 500);
	if ( _taccess(str, 0) != 0)
	{
		// if there is not folder created - create it!
		CreateDirectory(str, NULL); 

		if ( _taccess(str, 0) != 0) 
		{

			printf_log("GetFileName error creates logs folder");

			// oops - there is no access to this folder.
			// create 
			GetMyPath(str, false, NULL);		
			strcat(str, "logs\\");
			CreateDirectory(str, NULL);
			WriteReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "LogFilesPath", str);
		}
	}
	
	strcat(str, username);		
	CreateDirectory(str, NULL);	
	strcat(str, "\\");	
	strcat(str, date);	

	if(i==2)
	{
		wsprintf(str+lstrlen(str),"[%i %i]",tm.wHour,tm.wMinute);
	}
	strcat(str, ext);

	int i2=1;

	if(i==2)
	{

		// if file exists no action
		while (_taccess(str, 0)==0 ) {
			
			// file exists
			ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "LogFilesPath", str, 500);			
			
			strcat(str, username);				
			strcat(str, "\\");	
			strcat(str, date);	
			
			if(i==2)
			{
				wsprintf(str+lstrlen(str),"[%i %i] %d",tm.wHour,tm.wMinute, i2);
			}
			strcat(str, ext);
			
			i2++;
		};


	}
	
}

#define BUFFSIZE 1300
char buffer[8500];

// write log into HTML file
bool WriteBufferToLogFile(char *message)
{
	
	HANDLE file; 
	DWORD wr;
	
	TCHAR file_name[700];		
	
	GetFileName(1, file_name, ".htm");
	
	if ( (file = CreateFile(file_name, GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 
		NULL,OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE ) 	
	{		
		
		return false;
	}
	
	if ( GetFileSize(file, NULL) < 10 ) {
		TCHAR html_header[200];
		strcpy(html_header, "<html><head><title>Log file");		
		strcat(html_header, "</title>\n <link rel=\"StyleSheet\" href=\"../../style.css\" type=\"text/css\" />  \n<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" > \n</head><body>\n");
		// write a header HTML 
		WriteFile(file, (LPVOID)html_header, strlen(html_header), (LPDWORD)&wr, NULL);
		
	}
	
	SetFilePointer(file, 0L, 0L, FILE_END);
	
	
	
	WriteFile(file, (LPVOID)&buffer, strlen(buffer), (LPDWORD)&wr, NULL);
	
	
	
	if ( message && strlen(message) >= BUFFSIZE ) 
	{
		
		
		WriteFile(file, (LPVOID)buffer, strlen(buffer), (LPDWORD)&wr, NULL);
	}
	
	CloseHandle(file); 
	buffer[0]=0;
	return true;
}


// Add a message to the LOG BUFFER file
// if it overloads BUFFSIZE then writes buffer to the actual LOG file
void WINAPI AddLOG_message_me(char *message, int len, int UTF8encode/*add_time*/)
{			    
	
	try {
		
		char time[100];
		
		SYSTEMTIME	systime;  					

		
		if (message == NULL )// clear buffer after hibernation.
		{
			if (len==1)
			{
				 // save data
				WriteBufferToLogFile(NULL);
			}

			buffer[0]=0; buffer[1]=0;
			
			
			return;
			
		}
			
		WCHAR _unicode_buff[4000];				
		char _utf8_message[4000];				
		char *pszMessage = message;

		if (UTF8encode)
		{
			MultiByteToWideChar(CP_ACP , 0, message, -1, _unicode_buff, 3900);
			WideCharToMultiByte(CP_UTF8, 0, _unicode_buff, -1, _utf8_message, 3900, NULL, NULL);
			pszMessage = _utf8_message;
		}
		
		if ( strlen(buffer) + strlen(pszMessage) >= BUFFSIZE )	  
		{			
			if ( WriteBufferToLogFile(pszMessage) == false)
			{
				// the log file itself  maybe temporarily inaccessible... 
				if ( pszMessage && strlen(buffer)  < 3000)
					strcat(buffer, pszMessage);					
				return;

			}
		}		
				
		if ( strlen(pszMessage) < BUFFSIZE )
			strcat(buffer, pszMessage);	
		
		return;
		
	}
	
	catch (...)
	{
		
	}
	
}


// add current time to LOG
void AddLOG_time()
{
	if (_log_app_duration  ) // we track time periods - we don't need to track Time Marks here...
		return;

	CTime currtime = CTime::GetCurrentTime();

	CString str;	
	

	
	str = currtime.Format(STR_FORMAT_TIME_MARK);
	AddLOG_message( (char*)(LPCTSTR)str, 0, 0);		
		
	return;
	

}

// Send or Upload current HTML Log data now
bool UploadLogDataNow(int flush_buffer =1)
{	
	printf_log("UploadLogDataNow ");

	if (flush_buffer)
	{
		AddLOG_message2("", "", 5);  // lets stop active App log (this will allso stop URL). this will also call AddLOG_message()
		if (_log_app_duration)
			WriteAllDurations();
		else
			AddLOG_message(NULL, 1, 0); // flush user activity log buffer into a file

		/* reset "App monitor"  */		
		curr_wnd = (HWND)0xFFFFFF;  
		_currentWindowName[0]=0;			
		_current_pid =0;				
		 /*------------------- */	
	}

	TCHAR devid[600], name[700];
	ReadRegAny(TEXT("SOFTWARE\\StaffCounter") , "smtp_to", devid, 500);
	GetFileName(1, name, ".htm");		

	report newReport;
	newReport.send_report("web_post", devid, "", name);

	return true;
}

// time tracking globals


//time tracking array
typedef struct _APP_STR {
	 int type;		//Log item type (app, url ...)
	 CString str; // app name, or URL title
	 CString attr; // 
	 DWORD dur;	// time usage - seconds spend in this App/Url
	 CString time; // 
} APP_STR, *PAPP_STR;

int _app_logs_max = 100; // keep 100 apps/urls max
APP_STR _app_logs[211]; // array to remember each App/Url time usage

int _last_app_id[20]={-1,-1,-1,-1,-1,  -1,-1,-1,-1,-1, -1,-1,-1,-1,-1, -1,-1,-1,-1,-1 };
DWORD _ticks_app_started[20]={0 };


/*
Write all durations from _app_logs into HTML file. 
then clear _app_logs array
*/
void WriteAllDurations()
{
	CString html_tag;

	// check if the Durations Array is empty?
	if (_app_logs[0].attr.GetLength() == 0)
		return;

	printf_log("ALD flush " );
	int total_secs =0;
	int x=0;

	for (x =0; (x<_app_logs_max  && _app_logs[x].attr.GetLength()!=0); x++)
	{
		

		total_secs += _app_logs[x].dur; // Remember and count Total time usage (for testing and measurements)

		// create HTML tag for each element and Write into HTML
		if (_app_logs[x].type == 5 ) // app
			html_tag.Format(STR_FORMAT_APP, (LPCTSTR)_app_logs[x].time, _app_logs[x].dur, (LPCTSTR)_app_logs[x].attr, (LPCTSTR)_app_logs[x].str );

		if (_app_logs[x].type == 15 ) // url
			html_tag.Format(STR_FORMAT_URL, (LPCTSTR)_app_logs[x].time, _app_logs[x].dur, (LPCTSTR)_app_logs[x].attr, (LPCTSTR)_app_logs[x].str );

		

		AddLOG_message((char*)(LPCTSTR)html_tag, 0, 0); // APP items allready UTF8 encoded 

		// remove this entry from array
		_app_logs[x].attr = "";
		_app_logs[x].str = "";
		_app_logs[x].type = 0;
		_app_logs[x].dur = 0;

	}

	_last_app_id[5] = -1;
	_last_app_id[15] = -1;

	total_mins_session += total_secs; // Remember and count Total time usage (for testing and measurements)
	printf_log("ALD done.  min: %d (for %d apps). Total Sec: %d ", total_secs / 60, x, total_mins_session);
	g_total_sec_for_period = 0;

	AddLOG_message(NULL, 1, 0); // flush user activity log buffer into a file

}

// add document name to durations array 
// attr - document name
// str?
void AddLOG_duration_item(CString str, CString attr, int type, CString time)
{
	bool existing_found =0;

	// find current app by attr and remember it
	int x =0;
	for (x =0; (x<_app_logs_max  && _app_logs[x].attr.GetLength()!=0); x++)
	{
		// _app_logs[x].str - exact document name
		// str - Window title with document name
		if ( type == _app_logs[x].type && str.Find(_app_logs[x].str)>=0  )
		{
			// update 
			existing_found = true;
			break;
		}
	}

	// this is new App/Url create new item for it 
	if ( existing_found == false )
	{		
		printf_log(" new Doc = %s", (LPCTSTR)str);

		// create new 
		_app_logs[x].attr = " ";
		_app_logs[x].str = str;
		_app_logs[x].time = time;
		_app_logs[x].dur = 0;
		_app_logs[x].type = type;		
	}	
	
}

// Remember when the App (str) started and calculate the time it was active
// str - app/url name
// attr - app title or url title
// type - 5 means Apps, 15 means URLs
// time - time as string
//
void AddLOG_duration(CString str, CString attr, int type, CString time)
{
	bool existing_found = false;
	int active_seconds =0;
	int last_app_secs = 0;

	if (_last_app_id[type] >=0) // first we update previlious app time usage
	{
		
		int interval_milis = GetTickCount() - _ticks_app_started[type];
		active_seconds = ( interval_milis / 1000);

		if ( interval_milis % 1000 > 500)
			active_seconds++;
		
		if (active_seconds > 60 * 60 * 9) /*8 hours working non stop in 1 app - looks like err*/
		{ 
			// time usage > 40mins this should NOT happend. Logical Error
			printf_log("ALD ERR upd = %d %s err active_seconds - %d ", _last_app_id[type], (LPCTSTR)_app_logs[_last_app_id[type]].attr, active_seconds);
			return;
		} else
		{
			g_total_sec_for_period += active_seconds; // remember totals sec for period.
			_app_logs[_last_app_id[type]].dur += active_seconds;
			last_app_secs = _app_logs[_last_app_id[type]].dur;
			printf_log("ALD upd = %s %d sec ", (LPCTSTR)_app_logs[_last_app_id[type]].attr, _app_logs[_last_app_id[type]].dur);
		}

	}

	// just stop time counter for the last used app (User is Idle now, Session Ends or Restarting...)
	if (attr.GetLength() == 0 )
	{
		if ( _last_app_id[type] != -1 )
		{
			printf_log("ALD stop %s (%d sec)",  (LPCTSTR)_app_logs[_last_app_id[type]].attr, _app_logs[_last_app_id[type]].dur);
			_last_app_id[type] = -1;			
		}
		return;
	}

	_ticks_app_started[type] = GetTickCount();


	// find current app by attr and remember it
	int x =0;
	for (x =0; (x<_app_logs_max  && _app_logs[x].attr.GetLength()!=0); x++)
	{
		if ( _app_logs[x].attr == attr && type == _app_logs[x].type)
		{
			// update 
			existing_found = true;
			break;
		}
	}	
	_last_app_id[type] = x;

	// this is new App/Url create new item for it 
	if ( existing_found == false )
	{
		_last_app_id[type] = x;
		
		printf_log("ALD new = %d %s", _last_app_id[type], (LPCTSTR)attr);
		// create new 
		_app_logs[x].attr = attr;
		_app_logs[x].str = str;
		_app_logs[x].time = time;
		_app_logs[x].dur = 0;
		_app_logs[x].type = type;		
	}	

	if ( g_total_sec_for_period > g_max_work_mins_upload ) // we got 10 working minutes
	{
		// Write All logs now!
		WriteAllDurations();

		_last_app_id[type] = 0;
		_app_logs[0].attr = attr;
		_app_logs[0].str = str;
		_app_logs[0].time = time;
		_app_logs[0].dur = 0;
		_app_logs[0].type = type;	
	}

}

// main function to add an entry to the HTML LOG
//
void AddLOG_message2(CString str, CString attr, int type)
{
	CString html_tag;
	CString time;
	int UTF8encode=true;

	if (str.GetLength() && attr.GetLength())
	{
		CTime currtime = CTime::GetCurrentTime();
		time = currtime.Format(STR_FORMAT_TIME);		

		if (_log_app_duration == false) // add time into HTML
		{
			CString s;
			s.Format("%s %s", time, str);
			str = s;
		}
	}

		switch (type)
	{
		case 1:				
		break;

		case 2:
		break;

		// system
		case 4:
			html_tag.Format(STR_FORMAT_SYSTEM, time, str );
		break;

		// log Application usage 
		case 5:		
			
			if (false == _log_app_duration  ) // plain style log
			{
				UTF8encode=false;
				if (str.GetLength())
					html_tag.Format(STR_FORMAT_APP0, time, attr, str );
				break;
			}

			AddLOG_duration("", "", 15, time);		// App switch - stop active URL dration also!
			AddLOG_duration(str, attr, type, time);	
			return;
			
		break;

		// folder
		case 10:
			html_tag.Format("<p class=\"folder\" time=\"%s\" >%s</p>", time, str );
		break;

			// url
		case 15:

			if (false == _log_app_duration  ) // plain style log
			{
				if (str.GetLength())
					html_tag.Format(STR_FORMAT_URL0,  time,attr, str );
				break;
			}

			AddLOG_duration("", "", 5, time); // URL switch - stop active app duration also!
			AddLOG_duration(str, attr, type, time);
			return;
			break;

		// StaffCounter bug
		case 17:
			printf_log("StaffCounter BUG %s : %s", time, str);
			
		break;

		// idle time
		case 18:
			{
					

				html_tag.Format(STR_FORMAT_IDLE, time,  str, attr );
			}
		break;

		// flush data to file
		case 77:
			AddLOG_message(NULL, 1, 0); // flush data to a file
			return;
		
	}

	if (html_tag.GetLength() == 0)
		return;

	html_tag += "\n";

	AddLOG_message((char*)(LPCTSTR)html_tag, 0, UTF8encode);
}


/////////////////////////////////////////////////////////////////////////////
// CMainWndDlg dialog

CMainWndDlg::CMainWndDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMainWndDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMainWndDlg)
	_log_allusers = FALSE;
	pWnd = this;
	//}}AFX_DATA_INIT
	// Note that LoadIcon does not require a subsequent DestroyIcon in Win32
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);

	
	ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter"), "ProductName", _ProductName, 90);

	if (strlen(_ProductName) == 0)
	{
		_tcscpy(_ProductName, ::LS("StaffCounter") );
	}

	_product_staff = 0;

		
	if ( strcmp(_ProductName, "StaffCounter") == 0 )
		_product_staff = 1;
		

	logBrowserTitleAsURL = 1;

	
	_start_minimized = false;
	_working = false;
	_log_allusers = ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "AllUsers", 0 );	

	hwndNextViewer  = NULL;

	_resume_time = 0;
}

void CMainWndDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMainWndDlg)
	DDX_Control(pDX, IDC_STATIC3, _ads);
	
	//}}AFX_DATA_MAP

	//DDX_Check(pDX, IDC_CHECK1, _log_allusers);
}

BEGIN_MESSAGE_MAP(CMainWndDlg, CDialog)
	//{{AFX_MSG_MAP(CMainWndDlg)
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BUTTON3, OnStop)
	ON_BN_CLICKED(IDC_STATIC3, OnStatic3)
	ON_WM_CTLCOLOR()
	ON_BN_CLICKED(IDC_STATIC2, OnOpenLogFolder)
	ON_BN_CLICKED(IDC_STATIC1, OnClearLogs)
	ON_WM_ENDSESSION()
	ON_WM_SYSCOMMAND()
	ON_WM_DRAWCLIPBOARD()
	ON_WM_TIMER()
	//}}AFX_MSG_MAP
	ON_WM_DEVICECHANGE()
	ON_MESSAGE( WM_POWERBROADCAST, onPowerChanges)
	ON_WM_QUERYENDSESSION()
	ON_BN_CLICKED(IDCANCEL, &CMainWndDlg::OnBnClickedCancel)
	ON_REGISTERED_MESSAGE(WM_TASKBARCREATED,  OnTaskBarCreated)
	ON_BN_CLICKED(IDCANCEL1, &CMainWndDlg::OnBnClickedCancel1)
	
END_MESSAGE_MAP()

HWND hwnd1;

/////////////////////////////////////////////////////////////////////////////
// CMainWndDlg message handlers

#define GCL_HCURSOR         (-12)

BOOL CMainWndDlg::OnInitDialog()
{
	pMainWnd = this;
	printf_log("OnInitDialog. Release date:%S", __DATE__);	

#ifdef _X64
	printf_log("OnInitDialog. exit");	
	return TRUE;
#endif

	// Load UI strings

	::LS_UI(m_hWnd, IDC_STATIC9, "Start_Info" );
	::LS_UI(m_hWnd, IDC_BUTTON3, "Start" );

	::LS_UI(m_hWnd, IDC_STA1, "ViewLog_Info" );
	::LS_UI(m_hWnd, IDC_BUTTON1, "ViewLog" );
	::LS_UI(m_hWnd, IDC_STATIC1, "ClearLog" );
	::LS_UI(m_hWnd, IDC_STATIC2, "OpenLogFolder" );
	::LS_UI(m_hWnd, IDC_STA3, ("Options_Info") );
	::LS_UI(m_hWnd, IDC_BUTTON2, ("Options") );

	::LS_UI(m_hWnd, IDOK, ("Close") );

	_log_format_details_dur=0;

	

	if ( _product_staff )
	{
		// hide UI elements
		showItems(m_hWnd, 0, IDC_STATIC15, IDC_BUTTON4,  IDC_STATIC18, IDC_STA3, IDC_BUTTON2,  
			IDC_STATIC16, IDC_STA1, IDC_BUTTON1, IDC_STATIC1, IDC_STATIC2,   IDC_STATIC21,  IDC_STATIC22 ,  
			IDC_BUTTON3, 0);

		showItems(m_hWnd, 1, IDCANCEL1,  0);

		::LS_UI(m_hWnd, IDCANCEL1, ("Stop monitoring") );

		TCHAR ver[500];
		sprintf(ver, "StaffCounter v7 %s", __DATE__);
		SetDlgItemText(IDC_STATIC3, ver);
		SetDlgItemText(IDC_STA4, "");

		// setup logo
		char prg_path[100];	
		GetMyPath(prg_path, false);	
		strcat(prg_path, "logo.png");	
		CImage  img;
		img.Load(prg_path);	
		SendDlgItemMessage(IDC_STATIC17, STM_SETIMAGE, IMAGE_BITMAP , (LPARAM)img.Detach() ); 			

		
		
	} else
	{
		showItems(m_hWnd, 0, IDC_STATIC17, 0);
	}
	
	TCHAR WinCaption[200]={""};	
	strcpy(WinCaption, AfxGetApp()->m_pszAppName);					
	strcat(WinCaption, " []");	

	SetWindowText(WinCaption);

	verInfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&verInfo);

	if (true == _start_minimized) 
	{	

		CWnd::ModifyStyle(WS_VISIBLE, 0, 0);
		// we need to make it before OnInitDialog
	}



	CDialog::OnInitDialog();	

	HMODULE hMod_Kernel = LoadLibrary(_T("kernel32.dll"));		
	pWTSGetActiveConsoleSessionId = (WTSGetActiveConsoleSessionId_t)GetProcAddress(hMod_Kernel, "WTSGetActiveConsoleSessionId");

	GetMySessionID();

	hwnd1 = this->m_hWnd;

	// Set the icon for this dialog.  The framework does this automatically
	//  when the application's main window is not a dialog
	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	G_start_in_reg_mode  = _start_in_reg_mode; // start in Hidden more

	SetClassLong( _ads.m_hWnd, GCL_HCURSOR, (LONG)LoadCursor(NULL,IDC_HAND) );
	SetClassLong( GetDlgItem(IDC_STATIC1)->m_hWnd, GCL_HCURSOR, (LONG)LoadCursor(NULL,IDC_HAND) );
	SetClassLong( GetDlgItem(IDC_STATIC2)->m_hWnd, GCL_HCURSOR, (LONG)LoadCursor(NULL,IDC_HAND) );


	CDC *dc = GetDC();
	int nHeight = -MulDiv(8, GetDeviceCaps(dc->m_hDC, LOGPIXELSY), 72);
	HFONT hBoldFont = CreateFont(nHeight, 0, 0, 0, FW_NORMAL, 0, 1, 0,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, VARIABLE_PITCH|FF_SWISS, TEXT("MS Shell Dlg") );		

	SendDlgItemMessage( IDC_STATIC1, WM_SETFONT , (WPARAM)hBoldFont, MAKELPARAM(1,0) );
	SendDlgItemMessage( IDC_STATIC2, WM_SETFONT , (WPARAM)hBoldFont, MAKELPARAM(1,0) );

	// TODO: Add extra initialization here

	CreateToolTip( m_hWnd, IDC_STATIC1, "Delete all log files", 300, 0);
	CreateToolTip( m_hWnd, IDC_STATIC2, "Open folder that contains all log files for all users.", 300, 0);
	
	
	hWnd = m_hWnd;
	ReadReg( HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "selected_users", _logged_users, 390);
	
	SetTimer(51, 500, NULL);

	if (_start_in_reg_mode) 
		SetTimer(102, 7000, NULL);  // register as autorun

	
	DWORD sz=50;
	GetUserName( _username, &sz);
	BOOL reg_autorun = false;

	if (true == _start_minimized) 
	{
		ShowWindow(SW_HIDE);
	}
	 
	if (false == _start_minimized) 
	{
		ShowWindow(SW_SHOW);
		hwndNextViewer = 0;		

		if ( strlen(_logged_users) )
		{			
			SetDlgItemText(IDC_STATIC9, LS("Monitoring these users:"));

			if ( strstr(_logged_users, _username  ) ==0)			
			{
				showItems(m_hWnd, 1, IDC_BUTTON3, 0);
			}
		}

		// Read Autorun entry?
		TCHAR szModuleName[200] = {""};
		ReadReg(HKEY_LOCAL_MACHINE, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, szModuleName, 190 );
		if (strlen(szModuleName) == 0 )
			ReadReg(HKEY_CURRENT_USER, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, szModuleName, 190 );

		if (strlen(szModuleName) )
		{
			// StaffCounter is in Autostart - let's  Run it now also
			printf_log("OnInitDialog . RUN activated allready %s ",szModuleName );			
			reg_autorun = true;
			_working = true;
			SetDlgItemText(IDC_BUTTON3, LS("Stop monitoring"));
		}

		if ( _product_staff && reg_autorun == false ) 
		{
			_working = false;
			showItems(m_hWnd, 1, IDC_BUTTON3, 0); 
		}

	}

	if (true == _start_minimized || reg_autorun) 
	{	
		if ( strlen(_logged_users) && _log_allusers == false)
		{		
			// is current user name in the list??
			if ( strstr(_logged_users, _username  ) ==0)
			{
				 // we do not monitor this user
				printf_log("OnInitDialog do not start for this user %s", _username );
				if (_start_minimized)
					PostQuitMessage(0);
				return false;
			}
		}

		StartLog_ReloadConfig();		
	}

	printf_log("OnInitDialog OK");



	if ( ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "display-mode", 0) == 1)
	{
		Tray_Start();
	}
		
	return TRUE;  // return TRUE  unless you set the focus to a control
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CMainWndDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// The system calls this to obtain the cursor to display while the user drags
//  the minimized window.
HCURSOR CMainWndDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CMainWndDlg::OnStart(BOOL reload_cfg/*=0*/)
{
	DWORD sz = 40;
	GetUserName(user_name, &sz);

	SystemParametersInfo(SPI_SETSCREENREADER, TRUE, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE); 
	 ::SendNotifyMessage( HWND_BROADCAST, WM_SETTINGCHANGE, SPI_GETSCREENREADER, 0);
	
	//URL hook - disabled because sometimes it detects all active URLs from the current web page.

	BOOL screenreader_running_ = false;
	SystemParametersInfo(SPI_GETSCREENREADER, 0, &screenreader_running_, 0);

	buffer[0]=0;
	
	AddLOG_message = AddLOG_message_me;

	
	// explicitly set SPI_SETSCREENREADER
	SetTimer( 101, 1000, NULL); 

	SYSTEMTIME times;	


	printf_log("OnStart 1");
	
		TCHAR str9[400];		
		sprintf(str9, "New session for %s", user_name);
		if (reload_cfg == 0)
			AddLOG_message2(str9, " ", 4);
	
	printf_log("OnStart OK");

	// send Data immediately once PC is ON! 

	return;
}

// Stop logger
void CMainWndDlg::OnStop_()
{				

	_working = false;

		SystemParametersInfo(SPI_SETSCREENREADER, FALSE, NULL, SPIF_SENDCHANGE);

	
		KillTimer( 10);
		KillTimer( 11);
		KillTimer( 12);
		KillTimer( 13);
		KillTimer( 14);
		KillTimer( 15);

		KillTimer( 16);
		KillTimer( 17);
		KillTimer( 18);
		KillTimer( 19);

		KillTimer( 101);
		KillTimer( 103);
		KillTimer( 104);

		return;
}


// RUN at startup
//
bool RegisterMySelf(bool remove, bool allusers) 
{	
    int nResultDll = 0;	
	HKEY hkRegKey;
	
	char szModuleName[512];
	GetModuleFileName(NULL, szModuleName, 512);    
	strcat(szModuleName, " -m");	

	if (remove)
	{
		WriteReg(HKEY_CURRENT_USER, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, (LPCTSTR)NULL);

		//if (allusers)
		WriteReg(HKEY_LOCAL_MACHINE, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, (LPCTSTR)NULL);

		return true;
	}	
	
	
	if (allusers)
	{
		WriteReg(HKEY_LOCAL_MACHINE, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, szModuleName );

		HKEY hKey;
		LONG ret;
		DWORD dwDisposition=0;

		TCHAR tmp_str1[500];
		ReadReg(HKEY_LOCAL_MACHINE, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, tmp_str1, 450);

		if ( strlen(tmp_str1) == 0 )
		{
			WriteReg(HKEY_CURRENT_USER, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, (LPCTSTR)NULL); // del this because  WriteReg() writes to CU  
			AfxMessageBox( LS("Please open as administrator to continue"), MB_ICONWARNING);
			return false;
		}

	} else
	{
		
		WriteReg(HKEY_CURRENT_USER, REGISTRY_APP_PATH, REGISTRY_KEY_NAME, szModuleName);	
	}	
	
	return true;
}


void CMainWndDlg::OnOK() 
{
	// TODO: Add extra validation here

	UpdateData(true);

	WriteReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "AllUsers", _log_allusers );

	if (!_working && _resume_time == 0) {
		PostQuitMessage(0);			
		Tray_Stop();
		printf_log("OnClose - %s, %s", _logged_users, _username );
		return;
	}	

	if ( strlen(_logged_users) ==0 )
	{
		ShowWindow(SW_HIDE);
		return;
	}

	if ( strstr(_logged_users, _username  ))			
	{
		ShowWindow(SW_HIDE);
		return;
	}

	printf_log("OnOk - %s, %s", _logged_users, _username );
	PostQuitMessage(0);
	Tray_Stop();

	CDialog::OnOK();
}

void CMainWndDlg::OnDestroy() 
{
	CDialog::OnDestroy();	

	printf_log("OnDestroy - %s, %s", _logged_users, _username );
	
	OnStop_();
	Tray_Stop();
	PostQuitMessage(0);	
}

void CMainWndDlg::OnStop() 
{
	UpdateData(true);
	WriteReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "AllUsers", _log_allusers );

	if (_working){
		
		RegisterMySelf(true, _log_allusers);  		// stop for all users

		AddLOG_message2("", "", 5);  // lets stop active App log (this will allso stop URL). this will also call AddLOG_message()
		if (_log_app_duration)
			WriteAllDurations();
		AddLOG_message2("Stop recording user activity journal.", " ", 4);		

		AddLOG_message(NULL, 1, 0); // flush user activity log buffer into a file

		UploadLogDataNow(0);
		OnStop_();
		
		SetDlgItemText(IDC_BUTTON3, LS("Start monitoring") );	

		if ( strlen(_logged_users) )
		{
			RegisterMySelf(true, 1);  		// stop for all users
			WriteReg( HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "selected_users", "");
		}
		AfxMessageBox( LS("Log was stopped") , MB_ICONINFORMATION);	
		
	} else {
		_log_allusers = ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "AllUsers", 0);
		
		// remember the list of selected users
		strcpy(_logged_users, "");
		for (int i=0; i<=8; i++)
		{
			TCHAR user[50];
			if ( Button_GetCheck( ::GetDlgItem(m_hWnd, idc_users_controls[i] ) ) )
			{
				GetDlgItemText(idc_users_controls[i], user, 50);
				if ( strlen(_logged_users) )
					strcat(_logged_users, ",");

				strcat(_logged_users, user); // UserA,UserB
			}
			
		}
		WriteReg( HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "selected_users", _logged_users);
		

		if ( strlen(_logged_users) && stricmp(_logged_users,_username ) ) // "this is not only Me"
		{
			if (false == RegisterMySelf(false, 1) )
				return;			
			
			LS_UI(m_hWnd, IDC_STATIC9, "Monitoring these users:");

			if ( strstr(_logged_users, _username  )==0 )
			{
				AfxMessageBox( LS("Monitoring was activated successfully"), MB_ICONINFORMATION);												
				SetDlgItemText(IDC_BUTTON3, LS("Stop monitoring") );	
				_working = true;
				// exit here because we don't need to monitor current user
				return ;
			}
		} else {
			if ( false == RegisterMySelf(false, _log_allusers) )
			return;

		}

		if (_log_allusers)
			LS_UI(m_hWnd, IDC_STATIC9, "Monitoring any user");		

		if (false == _log_allusers )
		{
			if ( strlen(_logged_users) ==0  )
				LS_UI(m_hWnd, IDC_STATIC9, "Monitoring current user");			

			if ( strlen(_logged_users) && stricmp(_logged_users,_username ) ==0 )
				LS_UI(m_hWnd, IDC_STATIC9, "Monitoring current user");			
		}

		
		SetDlgItemText(IDC_BUTTON3, LS("Stop monitoring") );	

		// check the registration... if "no access then"

		DeleteOldLogFiles();
		OnStart();
		ReloadConfig();

		printf_log("OnStop:: start OK");	
			
		AfxMessageBox( LS("Monitoring was activated successfully") , MB_ICONINFORMATION);	
	}	
}

void CMainWndDlg::OnCancel() 
{
	printf_log("OnCancel::");	

	if (!_working && _resume_time == 0) {
		PostQuitMessage(0);	
		Tray_Stop();
		return;
	}

	ShowWindow(SW_HIDE);	
}

void CMainWndDlg::OnStatic3() 
{
	// this is a release of StaffCounter...
	
	ShellExecute(NULL, "open", "http://staffcounter.net", NULL, NULL, SW_SHOWNORMAL | SW_RESTORE ); 
	return;
}

HBRUSH CMainWndDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor) 
{
	HBRUSH hbr = CDialog::OnCtlColor(pDC, pWnd, nCtlColor);

	int id = pWnd->GetDlgCtrlID();

	if (nCtlColor == CTLCOLOR_STATIC ) {		
		if (id == IDC_STATIC1 || id==IDC_STATIC2 || id==IDC_STATIC3)
			pDC->SetTextColor( RGB(0,0,228) );			
	}
		
	
	// TODO: Return a different brush if the default is not desired
	return hbr;
}


// Opens log files folder
void CMainWndDlg::OnOpenLogFolder() 
{
	char name[700];
	ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "LogFilesPath", name, 600);
	if ( _tcslen(name)==0){
		GetMyPath(name, false, NULL);		
	}
	ShellExecute(NULL, "open", name, NULL, NULL, SW_SHOW);	
}


// Delete all log files.
void CMainWndDlg::OnClearLogs() 
{
	
	WIN32_FIND_DATA FindFileData;
	char str[700];	
	char path[700];	
	char str2[700];	

	ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "LogFilesPath", str, 600);
	if ( _tcslen(str)==0){
		
		AfxMessageBox("This folder is not defined", MB_ICONINFORMATION);
		return;
	}

	if (AfxMessageBox("Are you sure to delete log files folder?", MB_YESNO) == IDYES)
	{

		str[strlen(str)-1]=0;

	// Delete folder with log files
			SHFILEOPSTRUCT lpFileOp;
			lpFileOp.wFunc = FO_DELETE;
			
			lpFileOp.pFrom = str;

			lpFileOp.pTo = NULL;
			lpFileOp.fFlags  = FOF_NOCONFIRMATION;
			lpFileOp.lpszProgressTitle = "Deleting log files...";
			lpFileOp.hwnd = m_hWnd; 
			SHFileOperation(&lpFileOp);

			AfxMessageBox("Clean logs done!", MB_ICONINFORMATION);
	}

	return;
}


LRESULT CMainWndDlg::onPowerChanges(WPARAM wp, LPARAM lp){
	printf_log("onPowerChanges %d", wp );

	if (wp == PBT_APMRESUMESUSPEND) 
	{
		DeleteOldLogFiles();	

		/* Reset Log files delivery... */
		int c = ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "smtp_interval", 0);

		SetTimer(14, c * 1000 * 60 , NULL); // email log file once, right after StartUp or Resume 
		if ( c && c>=5) {			
			SetTimer(16, 40 * 1000 , NULL); // email log file once, right after StartUp or Resume 
		}

		SetTimer(13, APP_MONITOR_TIMER_ELAPSE, NULL); // start "App monitor" 

		/* reset "App monitor"  */		
		curr_wnd = (HWND)0xFFFFFF;  
		_currentWindowName[0]=0;			
		_current_pid =0;				
		 /*------------------- */		
	}

	// Sleep or Notebook Lid is closed
	if (wp==PBT_APMSUSPEND )
	{		
		// stop "App monitor"  
		KillTimer(13); 	

		AddLOG_message2("", "", 5);  // lets stop active App log (this will allso stop URL). this will also call AddLOG_message()
		if (_log_app_duration)
			WriteAllDurations();
		else
			AddLOG_message(NULL, 1, 0); // flush user activity log buffer into a file

		UploadLogDataNow(0); 
		Sleep(3000);
		printf_log("UploadLogDataNow done");
		
	}

	return true;
}


void CMainWndDlg::OnEndSession(BOOL bEnding) 
{	
		
	UploadLogDataNow();


	CDialog::OnEndSession(bEnding);	
		
}

#include <Ntsecapi.h>		// Lsa API
#include <lm.h>				// Net API
#include <tchar.h>				// Net API


/////////////////////////////////////////////////////////////////////////////
// list of users who can log in remotely
ULONG CMainWndDlg::GetLocalUsers(/*CStringArray &arrUsers*/)
{
	// check whether we can find out about user privileges
	//BOOL bCanCheckRights = SeCheckUserRight(_T("Administrator"), SE_INTERACTIVE_LOGON_NAME);

	// list of all local users
	NET_API_STATUS	nNetStatus = 0;
	PUSER_INFO_1	pUsers = NULL, p;
	ULONG nUsersRead = 0, nUsersTotal = 0, nTargetUsers = 0;


	nNetStatus = NetUserEnum(NULL, 1, FILTER_NORMAL_ACCOUNT, (PBYTE *)&pUsers, MAX_PREFERRED_LENGTH,
		&nUsersRead, &nUsersTotal, NULL);
	if (NERR_Success != nNetStatus)
		return 0;

	ASSERT(nUsersRead == nUsersTotal);

	ULONG nAdminsIdx=0, nUsersIdx=0, nGuestsIdx=0, nIdx=0;				
	// put administrators in head of list,
	 //than users and guests in tail

	for(p = pUsers; nUsersRead >0 && nTargetUsers < 9; --nUsersRead, ++p)
	{
		TCHAR szUserName[128] = {""};			
		
		
		if (p->usri1_flags & (UF_LOCKOUT | UF_ACCOUNTDISABLE) )
			continue;

		WideCharToMultiByte(CP_ACP, 0, (LPCWSTR)p->usri1_name, -1, szUserName, 90, NULL, NULL);

		if( szUserName[0]=='_' )
			continue;

		if( _tcsstr(szUserName, "HomeGroup" ) )
			continue;

		switch(p->usri1_priv)
		{
		case USER_PRIV_ADMIN:
			nIdx = nAdminsIdx++; nUsersIdx++; nGuestsIdx++;
			break;
		case USER_PRIV_USER:
			nIdx = nUsersIdx++; nGuestsIdx++;
			break;
		case USER_PRIV_GUEST: default:
			nIdx = nGuestsIdx++;
			break;
		}
		nIdx++;

		SetDlgItemText(idc_users_controls[nTargetUsers], szUserName);
		showItems(m_hWnd, 1, idc_users_controls[nTargetUsers], 0);
		++nTargetUsers;
	}
	
	
	NetApiBufferFree(pUsers);

	return nTargetUsers;
}

void CMainWndDlg::FillUserCheckBoxes()
{
	ReadReg( HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "selected_users", _logged_users, 390);

	GetLocalUsers();

	for (int i=0; i<=8; i++)
	{
		TCHAR user[50];
		GetDlgItemText(idc_users_controls[i], user, 50);
		if ( strlen(user) && strstr(_logged_users, user  ) )
		{
			Button_SetCheck( ::GetDlgItem(m_hWnd, idc_users_controls[i] ) , 1);		
		}
	}
}



void CMainWndDlg::OnTimer(UINT_PTR nIDEvent) 
{

		report newReport;

	if (nIDEvent == 51) 
	{
		KillTimer(nIDEvent);
		FillUserCheckBoxes(); //set user names checkboxes
		
	}


	if (nIDEvent == 13) // record active window name
	{
		
		Monitor_ActiveWindowCaption();

	}

	if (nIDEvent == 14) // email log files each %% minute
	{

		TCHAR post_error[500];
		ReadReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter"), "web_post_error", post_error, 490);
		if ( strstr(post_error, "OK.") == 0 )
		{		
			printf_log("Upload Error: %s", post_error);		
			WriteReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter"), "web_post_error", "");
		}

		TCHAR devid[600], name[700];
		ReadRegAny(TEXT("SOFTWARE\\StaffCounter") , "smtp_to", devid, 500);
		GetFileName(1, name, ".htm");		

		report newReport;
		newReport.send_report("web_post", devid, "", name);

	}

	if (nIDEvent == 16) // email log file once again after 40 sec - then 2 minutes
	{
		KillTimer(nIDEvent); 

		SetTimer(17, 2 * 1000 * 60, NULL); 
		UploadLogDataNow();
	}


	if (nIDEvent == 17) // email log file once again after 2 minutes - then 2 minutes
	{
		KillTimer(nIDEvent); 

		UploadLogDataNow();	

	}

	if (nIDEvent == 18) // new day checking... 
	{
		CTimeSpan ts(0, 0, 0, 11); 
		CTime time1 = CTime::GetCurrentTime();	
		time1 += ts;

		// detect new day after Windows Sleep/Resume
		if (time1.GetDay() != last_10min_time.GetDay() ) 
		{
			last_10min_time = time1;
			printf_log("New day. ");
			total_mins_session = 0;
		}

		// detect new day after 23:59:50 
		if (time1.GetDay() != CTime::GetCurrentTime().GetDay()  )// after 10 sec new day?
		{
			printf_log("New day is coming... %s", time1.Format("%H:%M:%S") ); 			
			OnStop_();
			UploadLogDataNow();

			SetTimer(19, 12 * 1000, 0); // after 12 sec resume
		}

	}

	if (nIDEvent == 19) // resume Logging at new day
	{
		KillTimer(nIDEvent);

		printf_log("New day resume.." ); 
		OnStart(true); // reload cfg
		ReloadConfig();		
	}

	if (nIDEvent == 101)
	{
		// explicitly set SPI_SETSCREENREADER
		SystemParametersInfo(SPI_SETSCREENREADER, TRUE, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE); 
		::SendNotifyMessage( HWND_BROADCAST, WM_SETTINGCHANGE, SPI_GETSCREENREADER, 0);
		KillTimer(nIDEvent);
	}

	// delayed AutoStart registration 
	if (nIDEvent == 102)
	{
		RegisterMySelf(false, false);  	
		KillTimer(nIDEvent);
	}

	// Request New settings
	if (nIDEvent == 103)
	{
		
		TCHAR dev_id[200];
		ReadRegAny(TEXT("SOFTWARE\\StaffCounter") , "smtp_to", dev_id, 190);
		newReport.send_report("web_get_settings",dev_id, "", "" );

		SetTimer(104, 5000, NULL);		
	}

	// check for a new settings? - after settings request done!
	if (nIDEvent == 104)
	{
		KillTimer(nIDEvent);

		// check for a new settings?
		DWORD new_settings = ReadReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter") , "new-settings", 0);
		if (new_settings )
		{
			WriteReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter") , "new-settings", (DWORD)0);

			printf_log("ReloadConfig..."); 

			_working = false;
			OnStop_(); // this does save Current App Durations log 

			Sleep(100);

			OnStart(true); // reload cfg
			ReloadConfig();
			_working = true;			

		}
	}


	// check _resume_time  ///< at what day, hour, min - the StaffCounter should resume logging
	if (nIDEvent == 105)
	{
		CTimeSpan ts;
		ts = _resume_time - CTime::GetCurrentTime();
		int _minutes_before_resume = ts.GetTotalMinutes();

		CString s1;
		s1.Format("%d min before resume", _minutes_before_resume );
		printf_log(s1); 
		SetDlgItemText(IDC_STA4, s1);

		if (_minutes_before_resume <= 1)
		{
			KillTimer(nIDEvent);

			printf_log("resume.." ); 
			SetDlgItemText(IDC_STA4, "");

			Tray_Tip(_ProductName, "Time tracking resumed after a pause...");

			OnStart(true); // reload cfg
			ReloadConfig();			
			_working = true;
			SetDlgItemText(IDCANCEL1, "PAUSE for 30 min");

			_resume_time = 0;
		}
	}

	// Update Working Time label
	if (nIDEvent == 106)
	{
		if (_product_staff )
		{
			CString s1;
			int mins = total_mins_session / 60;
			if ( mins < 60)
				s1.Format("%d Mins for today", mins/ 60);
			else
				s1.Format("%dh %dm for today", mins/ 60 , mins% 60 );

			SetDlgItemText(IDC_STA4, s1);
			Tray_Tip(s1, "");
		}
	}
	
	CDialog::OnTimer(nIDEvent);
}


void CMainWndDlg::ReloadConfig()
{
	printf_log("ReloadConfig");

	_working = true;

	ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "report_cmdline", _report_cmd_line, 190);			

	_log_details = ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "LogDetails", 0);

	_log_urls = ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "LogURLs", 0);
	_log_details_url = 0; //log only domain name for URL

	// for staffcounter - log Windows titles
	_log_files_usage = ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "UsbMonitor", 0);	

	_log_app_duration = ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "LogAppDurations", 1);

	_log_details = 0;

	_log_app_duration = true;

	
	if ( 1 == ReadReg( HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "log_format", 0) )
	{
		_log_app_duration = true;
		_log_details = 0;
		_log_details_url = 0;
	}

	//  read config each 40 minutes
	if ( _product_staff && strstr(_report_cmd_line, "web_post"))
	{
		SetTimer(103, 40 * 60 * 1000, NULL); // each 40 sec
		SetTimer(106, 2  * 60 * 1000, NULL); // each 2 Min - read config
	}

	WriteReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter"), "web_post_error", "");
	WriteReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter"), "web_post_resp", "");
	WriteReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter"), "web_post_file", "");	
	WriteReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter"), "reject_html", "");	
	WriteReg(HKEY_CURRENT_USER, TEXT("SOFTWARE\\StaffCounter"), "dev_not_found", (DWORD)0);	

	last_10min_time = CTime::GetCurrentTime(); 

	

	
	int c = ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "smtp_interval", 0);

	if ( c ) 
	{
		SetTimer(14, c * 1000 * 60, NULL);
		if (c>=5)
			SetTimer(16, 40 * 1000 , NULL); // email log file once, right after StartUp or Resume 

	} else
		KillTimer(14);


	if ( ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "display-mode", 0) == 1)
	{
		Tray_Start();
	} else
		Tray_Stop();

	if ( ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "allow-app-stop", 0) == 1)
			showItems(m_hWnd, 1, IDCANCEL1,  /*IDCANCEL2,*/  0);
	else
		showItems(m_hWnd, 0, IDCANCEL1,  /*IDCANCEL2,*/  0);


	printf_log("OnStop:: ReloadConfig OK");

	SetTimer(13, APP_MONITOR_TIMER_ELAPSE, NULL); // check current window name; "App monitor" 

	SetTimer(18, 10 * 1000, NULL); // new day checking - each 10 sec
}



void GetMySessionID()
{
	HANDLE hProc  = NULL;
    HANDLE hToken = NULL;
	
	_session_id =0;

	DWORD session_id = 0xFFFFFF;	
	
	hProc = OpenProcess( PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());	
	
	// Reopen the process token now that we have added the rights to
	// query the token, duplicate it, and assign it.
	BOOL fResult = OpenProcessToken(hProc, TOKEN_QUERY, &hToken);
	if (FALSE == fResult)  
	{
		return;
	}
		
	DWORD len=0;
	
	if ( !GetTokenInformation(hToken, TokenSessionId , (LPVOID)&session_id, sizeof(DWORD), &len ) )
		return;
	

	if (session_id != 0xFFFFFF) {
		_session_id = session_id;
		
	}
	
	
	CloseHandle(hToken);	
	
	return;	
}

#include "Wtsapi32.h"

// TRUE - if current desktop not Winlogon 
bool isCurrentDesktopMy()
{	
	if ( GetSystemMetrics(SM_REMOTESESSION) == false)
	{
		if (pWTSGetActiveConsoleSessionId)
		{		

			if (_session_id != pWTSGetActiveConsoleSessionId())
				return false;
		} 

		HDESK desktop=OpenInputDesktop(0,0,0); 		
		if (desktop == NULL)
			return false;

		TCHAR desktopname[128] = {0}; 
		DWORD bytesread=120; 		
		GetUserObjectInformation(desktop,UOI_NAME,desktopname,128,&bytesread); 				
		CloseDesktop(desktop); 	

		if ( _tcsicmp(desktopname, _T("default"))==0 )
			return true;

	} else
	{

			LPTSTR ppBuffer = NULL;
			int session_state;
			DWORD session_state_len = 0;

			WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE, WTS_CURRENT_SESSION , WTSConnectState, &ppBuffer, &session_state_len );

			session_state = (int)*ppBuffer;

			WTSFreeMemory(ppBuffer);			

			if ( session_state != WTSActive /*|| WTSConnected*/)
			{				
				return false;
			}
			return true;
	}

	return false;
}

// if user is just typing URL - screenfetcher can wrongly detect partial url it.. to avoid we need to detect typing
BOOL IsTypingUrlNow(POINT *pt_url)
{
	POINT pt;
	
	{
		GUITHREADINFO gti = {0};
		gti.cbSize =sizeof(GUITHREADINFO);
		if ( GetGUIThreadInfo(NULL,&gti) && gti.rcCaret.left>1)
		{
			pt.x = gti.rcCaret.left;
			pt.y = gti.rcCaret.top;
			ClientToScreen(gti.hwndCaret, &pt);

			if ( abs( pt.y - pt_url->y ) < 20) // user edits URL now
			{
				printf_log("Typing caret2 %d %d (url %d %d)", pt.x, pt.y,  pt_url->x, pt_url->y);
				return true;
			}
		}
	}
	return false;
}

// cut to  domain.name
void cutURL(LPTSTR strURL)
{
	TCHAR strShortURL[200] = {0};


	// cut the URL into short one	

	LPCTSTR p = strstr(strURL, "www.");
	if (p )
		p+=4;
	else 
	{
		p = strchr(strURL, ':'); 
		if (p)
			p+=3;
	}

	if (!p)
		p = strURL;

	LPTSTR end = (LPTSTR)strchr(p, '/');

	if (end)
		*end =0;

	strncpy(strShortURL, p, 100);	
	strcpy(strURL, strShortURL);	
}

// if this is a search engine URL - 
//  https://www.google.md/search?q=test+search+&oq=test+search+&sugexp=chrome,mod=0&sourceid=chrome&ie=UTF-8
// convert into 
// https://www.google.md/search?q=test+search+
//
bool isThisURLSearch(LPTSTR strURL)
{
	LPCTSTR pQ = strstr(strURL, "&q=");
	if (!pQ)
		pQ =strstr(strURL, "?q=");

	if (!pQ)
		pQ =strstr(strURL, "text=");

	if (!pQ)
		pQ =strstr(strURL, "search?p=");

	if (  pQ ) 
	{
		// delete last unused URL params
		char* p = (char*)strstr(pQ+2, "&");

		if (p) 
			*p = 0;

		return true;

	} else
		return false;
}

bool CMainWndDlg::IsThisStringURL(LPCTSTR url) // return TRUE - if this string a URL
{	

	int url_len = strlen(url);
	if ( url_len < 5 || url_len > 480)
		return false;

	if ( strpbrk(url, " <>^") )
		return false;

	TCHAR strUrl[700];
	_tcsncpy(strUrl, url, 69);


	// '://' '.' and '/' are url parts
	int point_pos = 0; // should be in a middle 
	int colon_pos = 1;  // should be first
	int slash_pos= 100;  // should be last
	

	LPCTSTR p = strchr(strUrl, '.');
	if (p)
		point_pos = p-strUrl;	

	p = strstr(strUrl, "://");
	if (!p)
		p = strstr(strUrl, ":\\");
	if (p)
		colon_pos = p-strUrl;
	
	if (point_pos)
	{
		p = strchr(strUrl + point_pos, '/');
		if (p)
			slash_pos = p-strUrl;
	}

	if ( point_pos && point_pos > colon_pos && slash_pos > point_pos && slash_pos > colon_pos) // is this a URL??
	{
		// from http://www.rohos.com make rohos.com
		if ( (p=strstr(strUrl, "www.")) )		
			strcpy(strUrl, p + 4);
		else
		if ( (p=strstr(strUrl, "http:")) )		
			strcpy(strUrl, p + 5);		
		
		printf_log("%s %s", last_url, url);

		if (false == _log_details_url)
		{			
			if ( !isThisURLSearch((LPTSTR)url)) // if this is a search engine URL - leave search query
			{
				cutURL((LPTSTR)url);
			}
		}	

		if ( strncmp(last_url, url, URL_UI_MAX_LEN-1 )  ) // is it new url ?
		{	
			_tcsncpy(last_url, url, URL_UI_MAX_LEN);
			last_url[URL_UI_MAX_LEN]=0;
			


			if (_log_files_usage)// log URL with Browser caption (web site title)
			{
				WCHAR title[200]={0};
				TCHAR _utf8_txt[200]={0};
				::GetWindowTextW(::GetForegroundWindow(), title, 190); // get the Title of the current Window

				WideCharToMultiByte(CP_UTF8, 0, title, -1, _utf8_txt, 190, NULL, NULL);

				AddLOG_message2(_utf8_txt, url, 15); // url

			} else
				AddLOG_message2(url, url, 15); // url
			//DONE !

			TCHAR short_url[300] = {""};

			_tcscpy(short_url, last_url);			
			cutURL((LPTSTR)short_url);			

			return true;
		} 
		return true;
	}

	return false;
}



// get the Opera URL bar and gets its value
BOOL CMainWndDlg::Log_OperaURL(HWND wnd)
{
	IAccessible* pAcc = NULL;
	VARIANT varItem;

	POINT pt;
	RECT rect; 
	int try_again_cnt=0;
	::GetWindowRect(wnd, &rect);

	pt.x = rect.left + ((rect.right - rect.left ) / 3) ; // point in the middle 
	pt.y = rect.top + 45; // and a little bit lower (URL edit field)

try_again:

	pt.y += 10;
	if (try_again_cnt++ > 5)
		return false;

	HRESULT hr = AccessibleObjectFromPoint(pt,  &pAcc, &varItem);

	if ( (hr == S_OK) && (pAcc != NULL) )
	{
		BSTR bstrVal=0;
		
		hr = pAcc->get_accValue(varItem, &bstrVal);

		if (bstrVal)
		{
			char url[600], buff[900];
			wcstombs(url, bstrVal, 290);

			if ( IsTypingUrlNow(&pt) )
			{
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return false;
			}
		
			if ( IsThisStringURL(url ))
			{				
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return true;
			}
			SysFreeString(bstrVal);		  
			
		} else if (try_again_cnt > 4 && logBrowserTitleAsURL )
		{
			// log Tab name 
			TCHAR winTitle[100];
			::GetWindowText(wnd, winTitle, 90);
			hr = pAcc->get_accName(varItem, &bstrVal);

			if (bstrVal)
			{
				char url[600] = {"www."}, buff[900] = {0};

				WideCharToMultiByte(CP_ACP, 0, bstrVal, -1, url+4, 40, NULL, NULL);
				_tcscat(url, ".site");

				if (IsThisStringURL(url))
				{				
					SysFreeString(bstrVal);		  
					pAcc->Release();
					return true;
				}
				SysFreeString(bstrVal);		
			}
		}
		pAcc->Release();
	}
	goto try_again;
}


// get the FireFox URL bar and gets its value
BOOL CMainWndDlg::Log_FireFoxURL(HWND wnd)
{
	IAccessible* pAcc = NULL;
	VARIANT varItem;

	POINT pt;
	RECT rect;
	::GetWindowRect(wnd, &rect);

	pt.x = rect.left + ((rect.right - rect.left ) / 3) ; // point in the middle 
	pt.y = rect.top + 35; // and a little bit lower (URL edit field)

	int try_again_cnt=0;

try_again:

	
	pt.y += 9;
	if (try_again_cnt++ > 8)
		return false;

	HRESULT hr = AccessibleObjectFromPoint(pt,  &pAcc, &varItem);

	if ( (hr == S_OK) && (pAcc != NULL) )
	{
		BSTR bstrVal=0;
		
		hr = pAcc->get_accValue(varItem, &bstrVal);		

		if (bstrVal)
		{
			char url[600], buff[900];
			wcstombs(url, bstrVal, 290);			
			url[URL_UI_MAX_LEN+1]=0;

			if ( IsTypingUrlNow(&pt) )
			{
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return false;
			}

			if ( IsThisStringURL(url) )
			{				
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return true;
			}
			SysFreeString(bstrVal);		  						
		}
		pAcc->Release();
	}
	goto try_again;
}

// get the IE URL (MS EDGE is not supported) bar and gets its value
BOOL CMainWndDlg::Log_InternetExplorerURL(HWND wnd)
{
	IAccessible* pAcc = NULL;
	VARIANT varItem;

	POINT pt;
	RECT rect;
	::GetWindowRect(wnd, &rect);

	pt.x = rect.left + ((rect.right - rect.left ) / 4) ; // point in the middle 
	pt.y = rect.top + 30; // and a little bit lower (URL edit field)

	int try_again_cnt=0;

try_again:

	pt.y += 10;
	if (try_again_cnt++ > 8)
		return false;

	HRESULT hr = AccessibleObjectFromPoint(pt,  &pAcc, &varItem);

	if ( (hr == S_OK) && (pAcc != NULL) )
	{
		BSTR bstrVal=0;
		
		hr = pAcc->get_accValue(varItem, &bstrVal);

		if (bstrVal)
		{
			char url[600], buff[900];
			wcstombs(url, bstrVal, 290);

			if ( IsTypingUrlNow(&pt) )
			{
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return false;
			}

			if (IsThisStringURL(url))
			{				
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return true;
			}
			SysFreeString(bstrVal);		  			
		}
		pAcc->Release();
	}
	goto try_again;
}



// get the FireFox URL bar and gets its value
BOOL CMainWndDlg::Log_ChromeURL(HWND wnd)
{
	IAccessible* pAcc = NULL;
	VARIANT varItem;

	POINT pt;
	RECT rect;
	::GetWindowRect(wnd, &rect);

	pt.x = rect.left + ((rect.right - rect.left ) / 4) ; // point in the middle 
	pt.y = rect.top + 30; // and a little bit lower (URL edit field)

	int try_again_cnt=0;

try_again:

	pt.y += 4;
	if (try_again_cnt++ > 6)
		return false;

	HRESULT hr = AccessibleObjectFromPoint(pt,  &pAcc, &varItem);

	if ( (hr == S_OK) && (pAcc != NULL) )
	{
		BSTR bstrVal=0;
		
		hr = pAcc->get_accValue(varItem, &bstrVal);

		if (bstrVal)
		{
			char url[600], buff[900];
			wcstombs(url, bstrVal, 290);

			if ( IsTypingUrlNow(&pt) )
			{
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return false;
			}

			if (IsThisStringURL(url))
			{				
				SysFreeString(bstrVal);		  
				pAcc->Release();
				return true;
			}
			SysFreeString(bstrVal);		  			
		}
		pAcc->Release();
	}
	goto try_again;
}



// log a path of Windows Explorer Window
void CMainWndDlg::Log_Windows_Explorer_Path(HWND wnd)
{
	HWND wnd1;

	if ( IsWindowsVista() )
	{
		wnd1 = ::FindWindowEx(wnd, NULL, _T("WorkerW"), NULL);
		if (wnd1 ) {
			wnd1 = ::FindWindowEx(wnd1, NULL, _T("ReBarWindow32"), NULL);
			if (wnd1 ) {
				wnd1 = ::FindWindowEx(wnd1, NULL, _T("Address Band Root"), NULL);
				if (wnd1 ) {
					wnd1 = ::FindWindowEx(wnd1, NULL, _T("msctls_progress32"), NULL);					

					if (wnd1 ) {
						wnd1 = ::FindWindowEx(wnd1, NULL, _T("Breadcrumb Parent"), NULL);					

						if (wnd1 ) {
							wnd1 = ::FindWindowEx(wnd1, NULL, _T("ToolbarWindow32"), NULL);					


							char txt[600], buff[900];
							::GetWindowText(wnd1, txt, 200);

							if ( strcmp(_last_folder_path, txt ) ) // is it new path ?
							{
								_tcscpy(_last_folder_path, txt);
								AddLOG_message2(txt, " ", 10);
								
							}

							return;
						}
					}
				}
			}
		}

	} else

	{
		wnd1 = ::FindWindowEx(wnd, NULL, _T("WorkerW"), NULL);
		if (wnd1 ) {
			wnd1 = ::FindWindowEx(wnd1, NULL, _T("ReBarWindow32"), NULL);
			if (wnd1 ) {
				wnd1 = ::FindWindowEx(wnd1, NULL, _T("ComboBoxEx32"), NULL);
				if (wnd1 ) {
					wnd1 = ::FindWindowEx(wnd1, NULL, _T("ComboBox"), NULL);					

				}
			}
		}
	}

	if (wnd1)
	{
		IAccessible* pAcc = NULL;

		HRESULT hr = AccessibleObjectFromWindow(wnd1, OBJID_CLIENT,  IID_IAccessible , (void**)&pAcc);

		if ( (hr == S_OK) && (pAcc != NULL) )
		{
			BSTR bstrVal=0;
			VARIANT varChild;

			VariantInit(&varChild);
			varChild.vt =VT_I4;
			varChild.lVal =  CHILDID_SELF;
			hr = pAcc->get_accValue(varChild, &bstrVal);

			if (bstrVal)
			{
				char txt[600], buff[900];
	
				WideCharToMultiByte(CP_ACP, 0, bstrVal, -1, txt, 590, NULL, NULL);

				if ( strcmp(_last_folder_path, txt ) ) // is it new path ?
				{
					_tcscpy(_last_folder_path, txt);									
					AddLOG_message2(txt, " ", 10);
					
				}
				SysFreeString(bstrVal);		  
			}
			pAcc->Release();
		}
	}
}

// get current process name
BOOL GetProcessName(HWND wnd, LPTSTR process_name)
{
	// Get process name
		DWORD pid;
		TCHAR szProcessName[500] = "";
		TCHAR szAppName[500] = "";
		DWORD cchSize=490;
		GetWindowThreadProcessId(wnd, &pid);

		HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION ,   FALSE,pid);
		
		if (!hProcess)
			hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,   FALSE,pid);
		
		if (hProcess)
		{
			if (GetModuleFileNameEx(hProcess, (HMODULE)0, szProcessName, cchSize)        == 0) {
				if (GetProcessImageFileName(hProcess, szProcessName, cchSize) == 0 ) {
					_tcscpy(szProcessName, TEXT(""));
				}
			}

			if ( strlen(szProcessName) ) // delete Path and .exe from process name
			{
				LPCTSTR p = strrchr(szProcessName, '\\');
				if (p)
					strcpy(szAppName, p+1);
				else 
					strncpy(szAppName, szProcessName, 50);

				p = strstr(szAppName, ".exe");
				if (p)
					*((char*)p)=0;
			}

			CloseHandle(hProcess);
			strncpy(process_name, szAppName, 50);

			return true;
		} else
			return false;
}

// the basic function to get active App, URL, etc..
//
void CMainWndDlg::Monitor_ActiveWindowCaption()
{
	WCHAR parent[95] = {0};
	char text[700] = {""};
	WCHAR win_name[95] = {0};
	
	DWORD pid=0;
	
	strcpy(text, "");
	
	HWND wnd = ::GetForegroundWindow();	

	IsUserInputIdle();

	if ( false == isCurrentDesktopMy() ) 
	{
		if ( desktop_switch == false)
		{			
			desktop_switch = true;
		}
		return;
	}

	if (GetInputIdleSeconds() < 30 && _log_app_duration  == false)  // each 5 minutes log time
	{
		// log Time only if user input is active 
		CTime current_time = CTime::GetCurrentTime(); 
		CTimeSpan time_span;

		// each 5 minutes Log time
		time_span = current_time - last_10min_time;
		if (time_span.GetTotalMinutes() > 5 )
		{
			last_10min_time = CFileTime::GetCurrentTime(); 
			AddLOG_time();
		}
	}

	::GetWindowTextW(wnd, win_name, 90); // get the Title of the current Window
	if ( wcslen(win_name) < 1 ) return;		

	if ( ::IsWindowVisible(wnd) == false )
	{
		 // we do not log Invisible Windows - do not log app switch
		if (curr_wnd)
			return;

		wnd = HWND_DESKTOP; // if first window is invisible - log DESKTOP as first active window.
	}


	BOOL window_did_changed = false;

		// just log the entire process name		
		GetWindowThreadProcessId(wnd, &pid);

		if (pid != _current_pid)
		{
			window_did_changed = true;
			_current_pid = pid;

			// remove last URL history 
			last_url[0]=0;
		}

	if ( window_did_changed ) // Log Active Window name if it is changed 
	{ 
		
		
		wcscpy(_currentWindowName, win_name); 
		
		if (desktop_switch)
		{	

			desktop_switch = false;
		}		
		

		AddLOG_message(cr, 0, 0);
		
	
		::GetWindowTextW( ::GetParent(wnd), parent, 90); // get the Title of the parent Window (if its a dialog box)		

		// Get process name			

		if ( false == GetProcessName(wnd, _currentProcessName) )
		{
			// process name is a WIndow Title
			wcstombs(_currentProcessName, win_name, 50);
			_currentProcessName[15]=0;
		}		

		// log the new window now
	
		{
			
			WCHAR str[200]={0};
			TCHAR _utf8_txt[200]={0};

			if (_product_staff && _log_files_usage==false)
			{
				// don't log File names and Window captions
				strcpy(_utf8_txt, _currentProcessName);

			} else
			{				
				swprintf(str, L"%s", win_name);

				WideCharToMultiByte(CP_UTF8, 0, str, -1, _utf8_txt, 199, NULL, NULL);
			}			
			
			AddLOG_message2(_utf8_txt, _currentProcessName, 5); 	
			printf_log("new app: %s", (LPCTSTR)str);
		}		
		
		AddLOG_message(cr, 0, 0);		
		
	}	
	
	GetClassName(wnd, _current_class_name, 100);	

	BOOL detected_by_winclass = false;
	BOOL got_url = false;

	if (_log_urls)
	{		
		
		if ( stricmp(_current_class_name, "OperaWindowClass")==0 )
		{
			detected_by_winclass = true;
			got_url = Log_OperaURL(wnd);
		}

		if ( strstr(_current_class_name, "Mozilla") || strstr(_current_class_name, "Maxthon") )
		{
			detected_by_winclass = true;
			got_url = Log_FireFoxURL(wnd);
		}

		if ( strstr(_current_class_name, "IEFrame") )
		{
			detected_by_winclass = true;
			got_url = Log_InternetExplorerURL(wnd);
		}

		if ( strstr(_current_class_name, "Chrome") )
		{
			detected_by_winclass = true;
			got_url = Log_ChromeURL(wnd);	
		}

		if (got_url)
		{
			
		}
	}	
}

// delete old HTML files
void CMainWndDlg::DeleteOldLogFiles()
{
	int delete_logs_after_x_days =  ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "delete_logs_after_x_days", 4);

	if (delete_logs_after_x_days == 0)
		return;


	WIN32_FIND_DATA FindFileData;
	

	char username[100], str[510];
	DWORD sz=50;	

	GetUserName( username, &sz);	

	ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "LogFilesPath", str, 500);
	if ( _taccess(str, 0) != 0){
		GetMyPath(str, false, NULL);		
	}
	
	strcat(str, username);			
	strcat(str, "\\*.*");		
	
	SetCurrentDirectory(str);
	
	// delete old user logs	
	
	HANDLE h = FindFirstFile(str, &FindFileData);

	CFileTime current_time = CFileTime::GetCurrentTime(); 
	CFileTimeSpan time_span;

	ReadReg(HKEY_LOCAL_MACHINE, TEXT("SOFTWARE\\StaffCounter") , "LogFilesPath", str, 500);
	strcat(str, username);		
	strcat(str, "\\");		

	if (h == INVALID_HANDLE_VALUE)
		return;

	do {

		if ( strlen(FindFileData.cFileName) <=3)
			continue;

		//
		CFileTime ft(FindFileData.ftCreationTime);		
		time_span = current_time - ft;		

		if ( time_span.GetTimeSpan() > (CFileTime::Day * delete_logs_after_x_days) )
		{
			TCHAR path1[600];
			strcpy(path1, str);
			strcat(path1, FindFileData.cFileName);
			DeleteFile(path1);
		}

	} while ( FindNextFile(h, &FindFileData) );
}

bool CMainWndDlg::IsWindowsVista(void)
{
	if ( verInfo.dwPlatformId == VER_PLATFORM_WIN32_NT && 
		verInfo.dwMajorVersion == 6 )
		return true;

	return false;
}

BOOL CMainWndDlg::OnQueryEndSession()
{	
	
	return TRUE;
}

void CMainWndDlg::OnBnClickedCancel()
{
	// TODO: Add your control notification handler code here
	OnCancel();
}



// Create icon near clock
void CMainWndDlg::Tray_Start()
{

	NOTIFYICONDATA nf = {0};	
	nf.cbSize = sizeof(nf);
	nf.hWnd = m_hWnd;	
	nf.uID = 0;	
	nf.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;	
	

	nf.hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
 	strcpy(nf.szTip, _ProductName);
	nf.uCallbackMessage = WM_TRAY_MESSAGE;

	if ( Shell_NotifyIcon(NIM_ADD,&nf) == false )
	{	
		printf_log("Tray_Start failed");
	}
	return;
}

void CMainWndDlg::Tray_Stop()
{
	
	NOTIFYICONDATA nf;	
	nf.hWnd = m_hWnd;	
	nf.uID = 0;		
	nf.uCallbackMessage = WM_TRAY_MESSAGE;
	Shell_NotifyIcon(NIM_DELETE,&nf);			
}

void CMainWndDlg::Tray_Tip(CString tipTitle, CString tipText)
{

	NOTIFYICONDATA nf = {0};	
	nf.cbSize = sizeof(nf );
	nf.hWnd = m_hWnd;	
	nf.uID = 0;	
	nf.uFlags = NIF_INFO | NIF_MESSAGE ;
	nf.dwInfoFlags = NIIF_INFO;
	nf.uCallbackMessage = WM_TRAY_MESSAGE;

	if (tipText.GetLength() == 0 )
	{
		nf.uFlags = NIF_TIP;
		strcpy(nf.szTip, tipTitle);
		nf.dwInfoFlags = 0;
	}

	
	lstrcpyn(nf.szInfo, tipText, ARRAYSIZE(nf.szInfo));
	lstrcpyn(nf.szInfoTitle, tipTitle, ARRAYSIZE(nf.szInfoTitle));

	nf.uTimeout = 200; // in milliseconds

	Shell_NotifyIcon(NIM_MODIFY,&nf);		
}


// handle the 'Restart Explorer' event to reCreate tray icon
LRESULT CMainWndDlg::OnTaskBarCreated(WPARAM wp, LPARAM lp)
{
	if ( ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "display-mode", 0) == 1)
		Tray_Start( );

    return 0;
}


LRESULT CMainWndDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	// TODO: Add your specialized code here and/or call the base class

	// InnoSetup is going to update me...
	if (message==WM_CLOSEAPP_UPGRADE) {		
		printf_log("WM_CLOSEAPP_UPGRADE "); 

		AddLOG_message2("StaffCounter upgrade...", " ", 4);

		OnStop_(); // this does save Current App Durations log 
		UploadLogDataNow();

		PostQuitMessage(1);
		Tray_Stop();
		AddLOG_message2("Exit...", " ", 4);
		return 1;
	}

	if (message==WM_TRAY_MESSAGE) {		
		
		if ( (UINT)lParam == WM_LBUTTONDBLCLK) 	
		{										
			ShowWindow(SW_SHOW);				
		}												
		
		if ( lParam == WM_LBUTTONUP  || lParam == WM_RBUTTONUP) 	
		{													
			ShowWindow(SW_SHOW);								
		}														
			
		return 1;
	}

	if (message==WM_INPUT) {	

		BYTE buff[5000]={0};
		UINT dwSize=4900;

		
		if( GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buff, &dwSize, sizeof(RAWINPUTHEADER) ) )
		{
			PRAWINPUT raw=(PRAWINPUT)buff;
		}
	}

	return CDialog::WindowProc(message, wParam, lParam);
}

// Pause monitoring 
void CMainWndDlg::OnBnClickedCancel1()
{
	
	if ( ReadRegAny( TEXT("SOFTWARE\\StaffCounter") , "allow-app-stop", 0) == 0)
	{
		AfxMessageBox("This operation is not allowed by Administrator",  MB_ICONINFORMATION);
		return;
	}	

	if (/*_working == false*/ _resume_time != 0)
	{
		printf_log("resume.." ); 
			SetDlgItemText(IDC_STA4, "");			

			OnStart(true); // reload cfg
			ReloadConfig();			
			
			_resume_time = 0;
			::LS_UI(m_hWnd, IDCANCEL1, ("Stop monitoring") );

			Tray_Tip(_ProductName, LS("Monitoring was activated successfully"));

			KillTimer(105);
			ShowWindow(SW_HIDE);

			return;
	}

	printf_log("Pause monitoring"); 
	AddLOG_message2("Stopped", " ", 4);		
	
	OnStop_(); // this does save Current App Durations log 
	UploadLogDataNow();

	CTimeSpan ts(0, 0, 30, 50); // 5 mins time span;
	_resume_time = CTime::GetCurrentTime();	
	_resume_time += ts;

	
	::LS_UI(m_hWnd, IDCANCEL1, ("Start monitoring") );

	printf_log("Pause monitoring  %s", _resume_time.Format("%H:%M")); 

	Tray_Tip(_ProductName, LS("Log was stopped"));

	Sleep(200);
	ShowWindow(SW_HIDE);	
}


// start log, set UI, delete old log files
int CMainWndDlg::StartLog_ReloadConfig(void)
{
	printf_log("StartLog_ReloadConfig");

	DeleteOldLogFiles();	
	OnStart(); // load Dll and setup log
	ReloadConfig();		

	SetDlgItemText(IDC_BUTTON3, LS("Stop monitoring") );
	SetDlgItemText(IDC_STATIC9, LS("Monitoring these users:"));

	if ( strlen(_logged_users) ==0 && false == _log_allusers )
		SetDlgItemText(IDC_STATIC9, LS("Monitoring current user") );

	if ( strlen(_logged_users) ==0 && _log_allusers )
		SetDlgItemText(IDC_STATIC9, LS("Monitoring any user"));
	
	return 0;
}

