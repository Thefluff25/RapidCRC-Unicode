/***************************************************************************
 Copyright 2004 Sebastian Ewert

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

***************************************************************************/

#include "resource.h"
#include "globals.h"
#include <process.h>
#include <commctrl.h>
#include "ed2k_hash.h"

#define SWAPBUFFERS() \
	tempBuffer=readBuffer;\
	dwBytesReadTb=dwBytesReadRb;\
	readBuffer=calcBuffer;\
	dwBytesReadRb=dwBytesReadCb;\
	calcBuffer=tempBuffer;\
	dwBytesReadCb=dwBytesReadTb

/*****************************************************************************
UINT __stdcall ThreadProc_Calc(VOID * pParam)
	pParam	: (IN/OUT) THREAD_PARAMS_CALC struct pointer special for this thread

Return Value:
	returns 0

Notes:
- walks trough the Fileinfo list and calculates CRCs
- if an error occured, GetLastError() is saved in the current pFileinfo->dwError
- what is has be calculated is sent to ThreadProc_Crc via pthread_params_crc->bCalculateCrc and
  pthread_params_crc->bCalculateMd5. The calling function has to implement the above logic
*****************************************************************************/
UINT __stdcall ThreadProc_Calc(VOID * pParam)
{
	THREAD_PARAMS_CALC * CONST pthread_params_calc = (THREAD_PARAMS_CALC *)pParam;
	// put thread parameter into (const) locale variable. This might be a bit faster
	CONST BOOL bCalculateCrc = pthread_params_calc->bCalculateCrc; // I hope this way less adress calculation has to be done
	CONST BOOL bCalculateMd5 = pthread_params_calc->bCalculateMd5;
	CONST BOOL bCalculateEd2k = pthread_params_calc->bCalculateEd2k;
	CONST HWND * CONST arrHwnd = pthread_params_calc->arrHwnd;
	SHOWRESULT_PARAMS * CONST pshowresult_params = pthread_params_calc->pshowresult_params;

	DWORD dwErrorCode = NO_ERROR;
	QWORD qwStart, qwStop, wqFreq;
	HANDLE hFile;
	BYTE *readBuffer = (BYTE *)malloc(MAX_BUFFER_SIZE_CALC);
	BYTE *calcBuffer = (BYTE *)malloc(MAX_BUFFER_SIZE_CALC);
	BYTE *tempBuffer;
	DWORD *dwBytesReadRb=(DWORD *)malloc(sizeof(DWORD));
	DWORD *dwBytesReadCb=(DWORD *)malloc(sizeof(DWORD));
	DWORD *dwBytesReadTb;
	BOOL bSuccess;
	BOOL bFileDone;

	FILEINFO * pFileinfo;

	HANDLE hEvtThreadMd5Go;
	HANDLE hEvtThreadEd2kGo;
	HANDLE hEvtThreadCrcGo;
	HANDLE hEvtThreadMd5Ready;
	HANDLE hEvtThreadEd2kReady;
	HANDLE hEvtThreadCrcReady;

	HANDLE hThreadMd5=NULL;
	HANDLE hThreadEd2k=NULL;
	HANDLE hThreadCrc=NULL;
	
	HANDLE hEvtReadyHandles[3];
	DWORD cEvtReadyHandles=0;

	THREAD_PARAMS_HASHCALC md5CalcParams;
	THREAD_PARAMS_HASHCALC ed2kCalcParams;
	THREAD_PARAMS_HASHCALC crcCalcParams;
	

	// set some UI stuff:
	// - clear the result windows
	// - clear the list window
	// - disable action buttons while in thread
	ListView_DeleteAllItems(arrHwnd[ID_LISTVIEW]);
	EnableWindowsForThread(arrHwnd, FALSE);
	ShowResult(arrHwnd, NULL, pshowresult_params);

	if(bCalculateCrc) {
		g_program_status.bCrcCalculated = TRUE;
		hEvtThreadCrcGo = CreateEvent(NULL,FALSE,FALSE,NULL);
		hEvtThreadCrcReady = CreateEvent(NULL,FALSE,FALSE,NULL);
		hEvtReadyHandles[cEvtReadyHandles] = hEvtThreadCrcReady;
		cEvtReadyHandles++;
		crcCalcParams.bFileDone = &bFileDone;
		crcCalcParams.hHandleGo = hEvtThreadCrcGo;
		crcCalcParams.hHandleReady = hEvtThreadCrcReady;
		crcCalcParams.buffer = &calcBuffer;
		crcCalcParams.dwBytesRead = &dwBytesReadCb;
	}

	if(bCalculateMd5) {
		g_program_status.bMd5Calculated = TRUE;
		hEvtThreadMd5Go = CreateEvent(NULL,FALSE,FALSE,NULL);
		hEvtThreadMd5Ready = CreateEvent(NULL,FALSE,FALSE,NULL);
		hEvtReadyHandles[cEvtReadyHandles] = hEvtThreadMd5Ready;
		cEvtReadyHandles++;
		md5CalcParams.bFileDone = &bFileDone;
		md5CalcParams.hHandleGo = hEvtThreadMd5Go;
		md5CalcParams.hHandleReady = hEvtThreadMd5Ready;
		md5CalcParams.buffer = &calcBuffer;
		md5CalcParams.dwBytesRead = &dwBytesReadCb;
	}

	if(bCalculateEd2k) {
		g_program_status.bEd2kCalculated = TRUE;
		hEvtThreadEd2kGo = CreateEvent(NULL,FALSE,FALSE,NULL);
		hEvtThreadEd2kReady = CreateEvent(NULL,FALSE,FALSE,NULL);
		hEvtReadyHandles[cEvtReadyHandles] = hEvtThreadEd2kReady;
		cEvtReadyHandles++;
		ed2kCalcParams.bFileDone = &bFileDone;
		ed2kCalcParams.hHandleGo = hEvtThreadEd2kGo;
		ed2kCalcParams.hHandleReady = hEvtThreadEd2kReady;
		ed2kCalcParams.buffer = &calcBuffer;
		ed2kCalcParams.dwBytesRead = &dwBytesReadCb;
	}

	QueryPerformanceFrequency((LARGE_INTEGER*)&wqFreq);

	pFileinfo = g_fileinfo_list_first_item;
	while(pFileinfo != NULL)
	{
		pthread_params_calc->pFileinfo_cur = pFileinfo;
		pthread_params_calc->qwBytesReadCurFile = 0;
		

        if ( (pFileinfo->dwError == NO_ERROR) && (pFileinfo->qwFilesize != 0) && (bCalculateCrc || bCalculateMd5 || bCalculateEd2k))
		{

			SetWindowText(arrHwnd[ID_EDIT_STATUS], pFileinfo->szFilename);

			QueryPerformanceCounter((LARGE_INTEGER*) &qwStart);
			hFile = CreateFile(pFileinfo->szFilename,
					GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN , 0);
			if(hFile == INVALID_HANDLE_VALUE){
				pFileinfo->dwError = GetLastError();
				continue;
			}

			bFileDone = FALSE;

			if(bCalculateCrc) {
				crcCalcParams.result = &pFileinfo->dwCrc32Result;
				hThreadCrc = CreateThread(NULL,0,ThreadProc_CrcCalc,&crcCalcParams,0,NULL);
			}
			if(bCalculateMd5) {
				md5CalcParams.result = &pFileinfo->abMd5Result;
				hThreadMd5 = CreateThread(NULL,0,ThreadProc_Md5Calc,&md5CalcParams,0,NULL);
			}
			if(bCalculateEd2k) {
				ed2kCalcParams.result = &pFileinfo->abEd2kResult;
				hThreadCrc = CreateThread(NULL,0,ThreadProc_Ed2kCalc,&ed2kCalcParams,0,NULL);
			}
			
			do {
				bSuccess = ReadFile(hFile, readBuffer, MAX_BUFFER_SIZE_CALC, dwBytesReadRb, NULL);
				if(!bSuccess) {
					pFileinfo->dwError = GetLastError();
					if(hThreadMd5)
						TerminateThread(hThreadMd5,1);
					if(hThreadEd2k)
						TerminateThread(hThreadMd5,1);
					if(hThreadCrc)
						TerminateThread(hThreadMd5,1);
					break;
				}
				pthread_params_calc->qwBytesReadCurFile  += *dwBytesReadRb; //for progress bar
				pthread_params_calc->qwBytesReadAllFiles += *dwBytesReadRb;
				
				WaitForMultipleObjects(cEvtReadyHandles,hEvtReadyHandles,TRUE,INFINITE);
				SWAPBUFFERS();
				if(*dwBytesReadCb<MAX_BUFFER_SIZE_CALC)
					bFileDone=TRUE;

				if(bCalculateCrc)
					SetEvent(hEvtThreadCrcGo);

				if(bCalculateMd5)
					SetEvent(hEvtThreadMd5Go);
				
				if(bCalculateEd2k)
					SetEvent(hEvtThreadEd2kGo);

			} while(bSuccess && (*dwBytesReadCb==MAX_BUFFER_SIZE_CALC));

			WaitForMultipleObjects(cEvtReadyHandles,hEvtReadyHandles,TRUE,INFINITE);

			if(hFile != NULL)
				CloseHandle(hFile);

			if(hThreadMd5)
				CloseHandle(hThreadMd5);
			if(hThreadEd2k)
				CloseHandle(hThreadEd2k);
			if(hThreadCrc)
				CloseHandle(hThreadCrc);

			QueryPerformanceCounter((LARGE_INTEGER*) &qwStop);
			pFileinfo->fSeconds = (float)((qwStop - qwStart) / (float)wqFreq);
		}
		else if(pFileinfo->qwFilesize == 0)	// this case is to have legal values in fSeconds
			pFileinfo->fSeconds = 0;			// if the file has 0 bytes

		
		InsertItemIntoList(arrHwnd[ID_LISTVIEW], pFileinfo);
		ShowResult(arrHwnd, pFileinfo, pshowresult_params);

		pFileinfo = pFileinfo->nextListItem;
	}

	if(bCalculateMd5) {
		CloseHandle(hEvtThreadMd5Go);
		CloseHandle(hEvtThreadMd5Ready);
	}
	if(bCalculateEd2k) {
		CloseHandle(hEvtThreadEd2kGo);
		CloseHandle(hEvtThreadEd2kReady);
	}
	if(bCalculateCrc) {
		CloseHandle(hEvtThreadCrcGo);
		CloseHandle(hEvtThreadCrcReady);
	}

	// enable action button after thread is done
	EnableWindowsForThread(arrHwnd, TRUE);

	PostMessage(arrHwnd[ID_MAIN_WND], WM_THREAD_CALC_DONE, 0, 0);
	
	_endthreadex( 0 );
	return 0;
}

/*****************************************************************************
UINT __stdcall ThreadProc_FileInfo(VOID * pParam)
	pParam	: (IN/OUT) THREAD_PARAMS_FILEINFO struct pointer special for this thread

Return Value:
returns 1 if something went wrong. Otherwise 0

Notes:
1) It checks that there are command line parameters
2) If there are, it reads the parameter either from the shell extension via Named Pipe
or it uses command line parameter as Input
3) In this step directories are expanded and filesizes, found CRC (from filename) are
get
4) At last it is checked if the first file is an SFV file. If so EnterSfvMode() is called
5) It sends an application defined window message to signal that is has done its job and
the CRC Thread can start
*****************************************************************************/
UINT __stdcall ThreadProc_FileInfo(VOID * pParam)
{
	THREAD_PARAMS_FILEINFO * CONST thread_params_fileinfo = (THREAD_PARAMS_FILEINFO *)pParam;
	INT iNumFiles;
	FILEINFO * pFileinfo;
	BOOL bCalculateCrc32, bCalculateMd5;
	// put thread parameter into (const) locale variable. This might be a bit faster
	CONST HWND * CONST arrHwnd = thread_params_fileinfo->arrHwnd;
	QWORD * CONST pqwFilesizeSum = thread_params_fileinfo->pqwFilesizeSum;
	SHOWRESULT_PARAMS * CONST pshowresult_params = thread_params_fileinfo->pshowresult_params;

#ifdef UNICODE
	LPTSTR* argv;
	INT argc;
	argv = CommandLineToArgvW(GetCommandLine(), &argc);
#else
	// There is no ANSI Version of CommandLineToArgvW, so we have to do it this way
	#define argc __argc
	#define argv __argv
#endif
    
	// is there anything to do? (< 2, because 1st element is the path to the executable itself)
	if(argc < 2){
		g_fileinfo_list_first_item = NULL;
		PostMessage(arrHwnd[ID_MAIN_WND], WM_THREAD_FILEINFO_DONE, 0, 0);
		return 0;
	}
	// use pipe input?
	if( lstrcmpi(argv[1], TEXT("-UsePipeCommunication")) == 0)
	{
		// pipe switches used by the shell extension
		if(argc > 2)
		{
			if(lstrcmpi(argv[2], TEXT("-CreateSFV")) == 0)
			{
				gCMDOpts = CMD_SFV;
			}
			else if(lstrcmpi(argv[2], TEXT("-CreateMD5")) == 0)
			{
				gCMDOpts = CMD_MD5;
			}
			else if(lstrcmpi(argv[2], TEXT("-PutNAME")) == 0)
			{
				gCMDOpts = CMD_NAME;
			}
			else if(lstrcmpi(argv[2], TEXT("-PutNTFS")) == 0)
			{
				gCMDOpts = CMD_NTFS;
			}
		}
		if(!GetDataViaPipe(arrHwnd)){
			g_fileinfo_list_first_item = NULL;
			PostMessage(arrHwnd[ID_MAIN_WND], WM_THREAD_FILEINFO_DONE, 0, 0);
		return 1;
		}
	}
	else // not using pipe input => command line parameter are files and folders
	{
		// get number of files
		iNumFiles = argc - 1; // -1 because 1st element is the path to the executable itself
		DeallocateFileinfoMemory(arrHwnd[ID_LISTVIEW]);
		AllocateMultipleFileinfo(iNumFiles);

		// setting the initial value of the filesize sum to 0
		(*pqwFilesizeSum) = 0;

		pFileinfo = g_fileinfo_list_first_item;
		for(INT i = 0; i < iNumFiles; ++i){
			StringCchCopy(pFileinfo->szFilename, MAX_PATH, argv[i+1]);
			pFileinfo = pFileinfo->nextListItem;
		}
	}

#ifdef UNICODE
	// in ANSI mode there is nothing to clean up
	GlobalFree(argv);
#endif

	PostProcessList(arrHwnd, pqwFilesizeSum, & bCalculateCrc32, & bCalculateMd5, pshowresult_params);

	if(gCMDOpts==CMD_MD5)
	{
		bCalculateCrc32 = FALSE;
		bCalculateMd5 = TRUE;
	}
	else if(gCMDOpts==CMD_SFV)
	{
		bCalculateCrc32 = TRUE;
		bCalculateMd5 = FALSE;
	}
	// tell Window Proc that we are done...
	PostMessage(arrHwnd[ID_MAIN_WND], WM_THREAD_FILEINFO_DONE, bCalculateCrc32, bCalculateMd5);

	_endthreadex( 0 );
	return 0;
}



DWORD WINAPI ThreadProc_Md5Calc(VOID * pParam)
{
	BYTE ** CONST buffer=((THREAD_PARAMS_HASHCALC *)pParam)->buffer;
	DWORD ** CONST dwBytesRead=((THREAD_PARAMS_HASHCALC *)pParam)->dwBytesRead;
	CONST HANDLE hEvtThreadReady=((THREAD_PARAMS_HASHCALC *)pParam)->hHandleReady;
	CONST HANDLE hEvtThreadGo=((THREAD_PARAMS_HASHCALC *)pParam)->hHandleGo;
	BYTE * CONST result=(BYTE *)((THREAD_PARAMS_HASHCALC *)pParam)->result;
	BOOL * CONST bFileDone=((THREAD_PARAMS_HASHCALC *)pParam)->bFileDone;

	MD5_CTX context;
	MD5_Init(&context);
	do {
		SignalObjectAndWait(hEvtThreadReady,hEvtThreadGo,INFINITE,FALSE);
		MD5_Update(&context, *buffer, **dwBytesRead);
	} while (!(*bFileDone));
	MD5_Final(result,&context);
	SetEvent(hEvtThreadReady);
	return 0;
}

DWORD WINAPI ThreadProc_Ed2kCalc(VOID * pParam)
{
	BYTE ** CONST buffer=((THREAD_PARAMS_HASHCALC *)pParam)->buffer;
	DWORD ** CONST dwBytesRead=((THREAD_PARAMS_HASHCALC *)pParam)->dwBytesRead;
	CONST HANDLE hEvtThreadReady=((THREAD_PARAMS_HASHCALC *)pParam)->hHandleReady;
	CONST HANDLE hEvtThreadGo=((THREAD_PARAMS_HASHCALC *)pParam)->hHandleGo;
	BYTE * CONST result=(BYTE *)((THREAD_PARAMS_HASHCALC *)pParam)->result;
	BOOL * CONST bFileDone=((THREAD_PARAMS_HASHCALC *)pParam)->bFileDone;

	CEd2kHash ed2khash;
	ed2khash.restart_calc();
	do {
		SignalObjectAndWait(hEvtThreadReady,hEvtThreadGo,INFINITE,FALSE);
		ed2khash.add_data(*buffer,**dwBytesRead);
	} while (!(*bFileDone));
	ed2khash.finish_calc();
	ed2khash.get_hash(result);
	SetEvent(hEvtThreadReady);
	return 0;
}

DWORD WINAPI ThreadProc_CrcCalc(VOID * pParam)
{
	BYTE ** CONST buffer=((THREAD_PARAMS_HASHCALC *)pParam)->buffer;
	DWORD ** CONST dwBytesRead=((THREAD_PARAMS_HASHCALC *)pParam)->dwBytesRead;
	CONST HANDLE hEvtThreadReady=((THREAD_PARAMS_HASHCALC *)pParam)->hHandleReady;
	CONST HANDLE hEvtThreadGo=((THREAD_PARAMS_HASHCALC *)pParam)->hHandleGo;
	DWORD * CONST result=(DWORD *)((THREAD_PARAMS_HASHCALC *)pParam)->result;
	BOOL * CONST bFileDone=((THREAD_PARAMS_HASHCALC *)pParam)->bFileDone;

	DWORD dwCrc32;
	DWORD * CONST pdwCrc32 = &dwCrc32;
	BYTE *bufferAsm;
	DWORD dwBytesReadAsm;

	// Static CRC table; we have a table lookup algorithm
	static CONST DWORD arrdwCrc32Table[256] =
	{
		0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
		0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
		0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
		0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
		0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
		0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
		0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
		0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
		0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
		0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
		0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
		0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
		0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
		0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
		0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
		0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,

		0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
		0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
		0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
		0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
		0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
		0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
		0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
		0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
		0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
		0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
		0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
		0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
		0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
		0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
		0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
		0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,

		0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
		0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
		0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
		0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
		0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
		0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
		0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
		0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
		0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
		0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
		0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
		0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
		0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
		0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
		0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
		0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,

		0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
		0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
		0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
		0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
		0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
		0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
		0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
		0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
		0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
		0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
		0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
		0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
		0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
		0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
		0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
		0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
	};
	// There is a bug in the Microsoft compilers where inline assembly
	// code cannot access static member variables.  This is a work around
	// for that bug.  For more info see Knowledgebase article Q88092
	CONST VOID * CONST ptrCrc32Table = &arrdwCrc32Table;
	dwCrc32 = 0xFFFFFFFF;

	do {
		SignalObjectAndWait(hEvtThreadReady,hEvtThreadGo,INFINITE,FALSE);
		
		bufferAsm = *buffer;
		dwBytesReadAsm = **dwBytesRead;

		// Register use:
		//		eax - CRC32 value
		//		ebx - a lot of things
		//		ecx - CRC32 value
		//		edx - address of end of buffer
		//		esi - address of start of buffer
		//		edi - CRC32 table
		//
		// assembly part by Brian Friesen
		__asm
		{
			// Save the esi and edi registers
			//push esi
			//push edi

			nop
			nop
			nop
			nop
			mov eax, pdwCrc32			// Load the pointer to dwCrc32
			mov ecx, [eax]				// Dereference the pointer to load dwCrc32

			mov edi, ptrCrc32Table		// Load the CRC32 table

			mov esi, bufferAsm			// Load buffer
			mov ebx, dwBytesReadAsm		// Load dwBytesRead
			lea edx, [esi + ebx]		// Calculate the end of the buffer

		crc32loop:
			xor eax, eax				// Clear the eax register
			mov bl, byte ptr [esi]		// Load the current source byte
			
			mov al, cl					// Copy crc value into eax
			inc esi						// Advance the source pointer

			xor al, bl					// Create the index into the CRC32 table
			shr ecx, 8

			mov ebx, [edi + eax * 4]	// Get the value out of the table
			xor ecx, ebx				// xor with the current byte

			cmp edx, esi				// Have we reached the end of the buffer?
			jne crc32loop

			// Restore the edi and esi registers
			//pop edi
			//pop esi

			mov eax, pdwCrc32			// Load the pointer to dwCrc32
			mov [eax], ecx				// Write the result
		}
	} while (!(*bFileDone));
	*result = ~dwCrc32;
	SetEvent(hEvtThreadReady);
	return 0;
}