// RTSP Video Application.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "RTSP Video Application.h"
#include <iostream>
#include <string>
#include <stdint.h>
#include <stdlib.h>
#include <fstream>
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs/imgcodecs.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/videoio/videoio.hpp"
#include "opencv2/imgproc/imgproc.hpp"

extern "C"
{
#include "avcodec.h"
#include "frame.h"
#include "avformat.h"
#include "swscale.h"
#include "avio.h"
#include "pixfmt.h"
}

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "opencv_core341d.lib")
#pragma comment(lib, "opencv_highgui341d.lib")
#pragma comment(lib, "opencv_imgcodecs341d.lib")
#pragma comment(lib, "opencv_videoio341.lib")

#define MAX_LOADSTRING 100


// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

bool recording;

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    Stream(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_RTSPVIDEOAPPLICATION, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);


    // Perform application initialization:

	if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_RTSPVIDEOAPPLICATION));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int) msg.wParam;
}

// Sets up window for stream
int setupWindow(std::basic_string<char, std::char_traits<char>, std::allocator<char>> RTSPAddress)
{
	cv::namedWindow(RTSPAddress, CV_WINDOW_NORMAL);
	cv::waitKey(30);	//Wait 30ms between frames
	return 0;
}

// Plays stream from RTSP Source by grabbing frames and displaying them one at a time
// playStream is called within a new thread every time
DWORD WINAPI playStream(LPVOID lpParameter)
{
	// Get address from user input
	std::string RTSPAddress(static_cast<char*>(lpParameter));

	//Check if window already exists
	if (cv::getWindowProperty(RTSPAddress, 0) < 0) {
		setupWindow(RTSPAddress);
	}

	av_register_all();
	SwsContext *img_convert_ctx;
	AVFormatContext* context = avformat_alloc_context();

	int video_stream_index = -1;

	avformat_network_init();

	//Open rtsp 
	if (avformat_open_input(&context, RTSPAddress.c_str(), NULL, NULL) != 0) {
		//Close window if can't connect
		cv::destroyWindow(RTSPAddress);
		MessageBox(NULL, L"Failed to connect", L"Error", MB_OK | MB_ICONERROR);
		return EXIT_FAILURE;
	}

	if (avformat_find_stream_info(context, NULL) < 0) {
		//Close window if can't find stream info
		cv::destroyWindow(RTSPAddress);
		MessageBox(NULL, L"Failed to find stream info", L"Error", MB_OK | MB_ICONERROR);
		return EXIT_FAILURE;
	}

	//Search video stream
	for (int i = 0; i < context->nb_streams; i++) {
		if (context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
			video_stream_index = i;
	}

	AVCodecContext *pCodecCtx = context->streams[video_stream_index]->codec;

	AVCodec *codec = NULL;
	//Find codec from video stream (H264/MPEG etc.)
	codec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (!codec) exit(1);

	AVCodecContext* ccontext = avcodec_alloc_context3(codec);

	AVPacket packet;
	av_init_packet(&packet);

	AVFormatContext* oc = avformat_alloc_context();

	AVStream* stream = NULL;

	//Start reading packets from stream
	av_read_play(context); //Play RTSP

	avcodec_get_context_defaults3(ccontext, codec);

	if (video_stream_index != -1) {
		avcodec_copy_context(ccontext, context->streams[video_stream_index]->codec);

		//Variables for recording stream
		std::ofstream myfile;
		cv::VideoWriter outputVideo;

		if (avcodec_open2(ccontext, codec, 0) < 0) exit(1);

		// Get context information from stream
		img_convert_ctx = sws_getContext(ccontext->width, ccontext->height, ccontext->pix_fmt, ccontext->width, ccontext->height,
			AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

		int size = avpicture_get_size(AV_PIX_FMT_RGB24, ccontext->width, ccontext->height);

		if (recording) {
			std::string url = RTSPAddress;
			size_t hostend;
			//Use hostname as name of .mov file
			if (url.find(":") != std::string::npos) {
				hostend = url.substr(7, url.length()).find_first_of(":");
			}
			else {
				hostend = url.substr(7, url.length()).find_first_of("/");
			}
			std::string host = url.substr(7, hostend);

			std::string filename = host.c_str();
			filename += ".mov";
			//Open video file with width and height of stream
			outputVideo.open(filename, -1, 10, cv::Size(ccontext->width, ccontext->height));
		}

		//Initialize and declare AVFrames and AVPictures
		uint8_t* picture_buf = (uint8_t*)(av_malloc(size));
		AVFrame* pic = av_frame_alloc();
		AVFrame* picrgb = av_frame_alloc();
		int size2 = avpicture_get_size(AV_PIX_FMT_RGB24, ccontext->width, ccontext->height);
		uint8_t* picture_buf2 = (uint8_t*)(av_malloc(size2));
		avpicture_fill((AVPicture *)pic, picture_buf, AV_PIX_FMT_YUV420P, ccontext->width, ccontext->height);
		avpicture_fill((AVPicture *)picrgb, picture_buf2, AV_PIX_FMT_RGB24, ccontext->width, ccontext->height);

		//While still receiving packets
		while (av_read_frame(context, &packet) >= 0)
		{
			if (packet.stream_index == video_stream_index) { //Packet is video
				if (stream == NULL)
				{ //Create stream in file
					stream = avformat_new_stream(oc, context->streams[video_stream_index]->codec->codec);
					avcodec_copy_context(stream->codec, context->streams[video_stream_index]->codec);
					stream->sample_aspect_ratio = context->streams[video_stream_index]->codec->sample_aspect_ratio;
				}
				int check = 0;
				packet.stream_index = stream->id;
				int result = avcodec_decode_video2(ccontext, pic, &check, &packet);
				//Scale image accordingly
				sws_scale(img_convert_ctx, pic->data, pic->linesize, 0, ccontext->height, picrgb->data, picrgb->linesize);
				//If window is crossed off then break and close the thread
				if (cv::getWindowProperty(RTSPAddress, 0) < 0) {
					break;
				}
				cv::Mat img(pic->height, pic->width, CV_8UC3, picrgb->data[0]);
				//Convert from BGR (FFMPEG) to RGB (OPENCV)
				cv::cvtColor(img, img, CV_BGR2RGB);
				if (recording) {
					outputVideo.write(img); //Write frame to .mov file
				}
				cv::imshow(RTSPAddress, img); //Display image on window and wait to display image
				cv::waitKey(1);
			}
			//Free packet variables
			av_free_packet(&packet);
			av_init_packet(&packet);
		}
		//Free Frame and Picture variables
		av_free(pic);
		av_free(picrgb);
		av_free(picture_buf);
		av_free(picture_buf2);
		if (recording) {
			outputVideo.release(); //Close VideoWriter
		}
		av_read_pause(context);
		avio_close(oc->pb);
		avformat_free_context(oc);
	}
	return 0;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_RTSPVIDEOAPPLICATION));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_RTSPVIDEOAPPLICATION);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      0, 0, 265, 100, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    switch (message)
    {
	case WM_CREATE:
	{
		HWND newStreamText = CreateWindow(L"STATIC",
			L"Press \"File\" to create a new stream",
			SS_LEFT | WS_CHILD,
			10, 10, 450, 30,
			hWnd,
			NULL,
			hInst, NULL);
		ShowWindow(newStreamText, 1);
		//Create DialogBox and link it to Stream Callback
		DialogBox(hInst, MAKEINTRESOURCE(IDD_STREAMBOX), hWnd, Stream);
	}
	break;
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
			case ID_STREAM:
			{
				DialogBox(hInst, MAKEINTRESOURCE(IDD_STREAMBOX), hWnd, Stream);
				break;
			}
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            // TODO: Add any drawing code that uses hdc here...
            EndPaint(hWnd, &ps);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

INT_PTR CALLBACK Stream(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		//Set initial text to example stream
		SetDlgItemText(hDlg, IDC_EDIT, L"rtsp://b1.dnsdojo.com:1935/live/sys3.stream");
		return (INT_PTR)TRUE;
		break;
	case WM_COMMAND:

		char RTSPAddress[200];

		if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		if (LOWORD(wParam) == IDNORECORDING)
		{
			recording = false;
			LPTSTR szText = new TCHAR[200];
			GetDlgItemText(hDlg, IDC_EDIT, szText, 200);
			//Convert LPTSTR to char *
			wchar_t* wRTSPAddress = szText;
			std::wcstombs(RTSPAddress, wRTSPAddress, 200);
			delete[] szText;
			//Open Window
			setupWindow(RTSPAddress);
			DWORD myThreadID;
			//Play Stream in new Thread
			HANDLE noRecordingHandle = CreateThread(0, 0, playStream, RTSPAddress, 0, &myThreadID);
			CloseHandle(noRecordingHandle);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		if (LOWORD(wParam) == IDRECORDING)
		{
			//Let other functions know that stream needs to be recorded
			recording = true;
			LPTSTR szText = new TCHAR[200];
			GetDlgItemText(hDlg, IDC_EDIT, szText, 200);
			wchar_t* wRTSPAddress = szText;
			std::wcstombs(RTSPAddress, wRTSPAddress, 200);
			delete[] szText;
			setupWindow(RTSPAddress);
			DWORD myThreadID;
			HANDLE RecordingHandle = CreateThread(0, 0, playStream, RTSPAddress, 0, &myThreadID);
			CloseHandle(RecordingHandle);
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}