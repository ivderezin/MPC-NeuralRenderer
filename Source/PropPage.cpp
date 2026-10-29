/*
 * (C) 2018-2026 see Authors.txt
 *
 * This file is part of MPC-BE.
 *
 * MPC-BE is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * MPC-BE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "stdafx.h"
#include "FrameGeneration/DlssSampleCapture.h"
#include "resource.h"
#include "Helper.h"
#include "DisplayConfig.h"
#include "PropPage.h"
#include "FilterInterfaces.h"
#include <shobjidl.h>


void SetCursor(HWND hWnd, LPCWSTR lpCursorName)
{
	SetClassLongPtrW(hWnd, GCLP_HCURSOR, (LONG_PTR)::LoadCursorW(nullptr, lpCursorName));
}

void SetCursor(HWND hWnd, UINT nID, LPCWSTR lpCursorName)
{
	SetCursor(::GetDlgItem(hWnd, nID), lpCursorName);
}

inline void ComboBox_AddStringData(HWND hWnd, int nIDComboBox, LPCWSTR str, LONG_PTR data)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_ADDSTRING, 0, (LPARAM)str);
	if (lValue != CB_ERR) {
		SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETITEMDATA, lValue, data);
	}
}

inline LONG_PTR ComboBox_GetCurItemData(HWND hWnd, int nIDComboBox)
{
	LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCURSEL, 0, 0);
	if (lValue != CB_ERR) {
		lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, lValue, 0);
	}
	return lValue;
}

void ComboBox_SelectByItemData(HWND hWnd, int nIDComboBox, LONG_PTR data)
{
	LRESULT lCount = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETCOUNT, 0, 0);
	if (lCount != CB_ERR) {
		for (int idx = 0; idx < lCount; idx++) {
			const LRESULT lValue = SendDlgItemMessageW(hWnd, nIDComboBox, CB_GETITEMDATA, idx, 0);
			if (data == lValue) {
				SendDlgItemMessageW(hWnd, nIDComboBox, CB_SETCURSEL, idx, 0);
				break;
			}
		}
	}
}


// CVRMainPPage

// https://msdn.microsoft.com/ru-ru/library/windows/desktop/dd375010(v=vs.85).aspx

CVRMainPPage::CVRMainPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"MainProp", lpunk, IDD_MAINPROPPAGE, IDS_MAINPROPPAGE_TITLE)
{
	DLog(L"CVRMainPPage()");
}

CVRMainPPage::~CVRMainPPage()
{
	DLog(L"~CVRMainPPage()");
}

void CVRMainPPage::SetControls()
{
	ComboBox_SelectByItemData(m_hWnd, IDC_UPSCALER, static_cast<unsigned>(m_neuralSettings.upscaler));
	ComboBox_SelectByItemData(m_hWnd, IDC_FRAME_GENERATOR, static_cast<unsigned>(m_neuralSettings.frameGenerator));
	ComboBox_SelectByItemData(m_hWnd, IDC_DLSS_MFG, m_neuralSettings.frameMultiplier);
	CheckDlgButton(IDC_NEURAL_ENABLED, m_neuralSettings.enabled ? BST_CHECKED : BST_UNCHECKED);
	ComboBox_SelectByItemData(m_hWnd, IDC_NR_STAGE, static_cast<unsigned>(m_neuralSettings.nrStage));
	CheckDlgButton(IDC_MFG_PRESENT_THREAD, m_neuralSettings.presentThread ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_FG_LIMIT_DISPLAY, m_neuralSettings.limitGenerationToDisplay ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_DLSS_FG_GUARD, m_neuralSettings.dlssFgGuard ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_ECHO_PIPELINE, m_neuralSettings.echoPipeline ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_AMD_BRIDGE, m_neuralSettings.amdBridge ? BST_CHECKED : BST_UNCHECKED);
	ComboBox_SelectByItemData(m_hWnd, IDC_MOTION_METHOD, static_cast<unsigned>(m_neuralSettings.motionMethod));
	ComboBox_SelectByItemData(m_hWnd, IDC_NEURAL_INTENSITY, m_neuralSettings.intensity);

	CheckDlgButton(IDC_CHECK1, m_SetsPP.bUseD3D11             ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK2, m_SetsPP.bShowStats            ? BST_CHECKED : BST_UNCHECKED);

	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO1, m_SetsPP.iTexFormat);

	CheckDlgButton(IDC_CHECK7, m_SetsPP.VPFmts.bNV12          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK8, m_SetsPP.VPFmts.bP01x          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK9, m_SetsPP.VPFmts.bYUY2          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK4, m_SetsPP.VPFmts.bOther         ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_COMBO9, CB_SETCURSEL, m_SetsPP.iVPDeinterlacing, 0);
	CheckDlgButton(IDC_CHECK3, m_SetsPP.bDeintDouble          ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK5, m_SetsPP.bVPScaling            ? BST_CHECKED : BST_UNCHECKED);
	SendDlgItemMessageW(IDC_COMBO8, CB_SETCURSEL, m_SetsPP.iVPSuperRes, 0);
	CheckDlgButton(IDC_CHECK19, m_SetsPP.bVPRTXVideoHDR       ? BST_CHECKED : BST_UNCHECKED);

	if (m_SetsPP.bHdrPassthrough) {
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO10, 0);
	} else if (m_SetsPP.bHdrLocalToneMapping) {
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO10, m_SetsPP.iHdrLocalToneMappingType);
	} else {
		ComboBox_SelectByItemData(m_hWnd, IDC_COMBO10, -1);
	}

	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO_DVTM, m_SetsPP.iHdrDoviToneMappingType);
	CheckDlgButton(IDC_CHECK20, m_SetsPP.bHdrLocalMeasurePeak ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK18, m_SetsPP.bHdrPreferDoVi       ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK14, m_SetsPP.bConvertToSdr        ? BST_CHECKED : BST_UNCHECKED);
	ComboBox_SelectByItemData(m_hWnd, IDC_COMBO_SDRTM, m_SetsPP.iSdrToneMappingType);

	SendDlgItemMessageW(IDC_COMBO7, CB_SETCURSEL, m_SetsPP.iHdrToggleDisplay, 0);
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETPOS, 1, m_SetsPP.iHdrOsdBrightness);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPOS, 1, m_SetsPP.iSDRDisplayNits / SDR_NITS_STEP);
	GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());

	CheckDlgButton(IDC_CHECK6, m_SetsPP.bInterpolateAt50pct   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK10, m_SetsPP.bUseDither           ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK17, m_SetsPP.bDeintBlend          ? BST_CHECKED : BST_UNCHECKED);

	CheckDlgButton(IDC_CHECK11, m_SetsPP.bExclusiveFS         ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK15, m_SetsPP.bVBlankBeforePresent ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK13, m_SetsPP.bAdjustPresentTime   ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(IDC_CHECK16, m_SetsPP.bReinitByDisplay     ? BST_CHECKED : BST_UNCHECKED);

	SendDlgItemMessageW(IDC_COMBO6, CB_SETCURSEL, m_SetsPP.iResizeStats, 0);

	SendDlgItemMessageW(IDC_COMBO5, CB_SETCURSEL, m_SetsPP.iChromaScaling, 0);
	SendDlgItemMessageW(IDC_COMBO2, CB_SETCURSEL, m_SetsPP.iUpscaling, 0);
	SendDlgItemMessageW(IDC_COMBO3, CB_SETCURSEL, m_SetsPP.iDownscaling, 0);
	SendDlgItemMessageW(IDC_COMBO4, CB_SETCURSEL, m_SetsPP.iSwapEffect, 0);

	m_SetsPP.iHdrDisplayMaxNits = discard<int>(m_SetsPP.iHdrDisplayMaxNits, HDR_NITS_DEF, HDR_NITS_MIN, HDR_NITS_MAX);
	SetDlgItemTextW(IDC_EDIT_DISPLAYMAX, std::to_wstring(m_SetsPP.iHdrDisplayMaxNits).c_str());
}

void CVRMainPPage::EnableControls()
{
#ifdef _WIN64
	const BOOL neuralAvailable = m_SetsPP.bUseD3D11 && IsWindows10OrGreater();
#else
	const BOOL neuralAvailable = FALSE;
#endif
	GetDlgItem(IDC_NEURAL_ENABLED).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_UPSCALER).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_FRAME_GENERATOR).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_DLSS_MFG).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_MFG_PRESENT_THREAD).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_FG_LIMIT_DISPLAY).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_ECHO_PIPELINE).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_DLSS_FG_GUARD).EnableWindow(neuralAvailable && m_neuralSettings.frameGenerator==processing::FrameGenerator::Dlss);
	GetDlgItem(IDC_DLSS_SAVE_SAMPLE).EnableWindow(neuralAvailable && m_neuralSettings.frameGenerator==processing::FrameGenerator::Dlss && m_neuralSettings.frameMultiplier>1);
	GetDlgItem(IDC_AMD_BRIDGE).EnableWindow(neuralAvailable);
	GetDlgItem(IDC_MOTION_METHOD).EnableWindow(neuralAvailable);
    for (int id : {IDC_SHADER_PRE_ADD, IDC_SHADER_PRE_CLEAR, IDC_SHADER_POST_ADD, IDC_SHADER_POST_CLEAR})
        GetDlgItem(id).EnableWindow(m_SetsPP.bUseD3D11);
	GetDlgItem(IDC_NEURAL_INTENSITY).EnableWindow(neuralAvailable && m_neuralSettings.enabled);
	GetDlgItem(IDC_NR_STAGE).EnableWindow(neuralAvailable && m_neuralSettings.enabled && !m_neuralSettings.echoPipeline);

	if (!IsWindows8OrGreater()) { // Windows 7
		const BOOL bEnable = !m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_STATIC1).EnableWindow(bEnable); // not working for GROUPBOX
		GetDlgItem(IDC_STATIC2).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK7).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK8).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK9).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK4).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK3).EnableWindow(bEnable);
		GetDlgItem(IDC_CHECK5).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC3).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO4).EnableWindow(bEnable);
	}
	else if (IsWindows10OrGreater()) {
		const BOOL bEnable = m_SetsPP.bUseD3D11;
		GetDlgItem(IDC_COMBO10).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC5).EnableWindow(bEnable);
		GetDlgItem(IDC_COMBO7).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC6).EnableWindow(bEnable);
		GetDlgItem(IDC_SLIDER1).EnableWindow(bEnable);
		GetDlgItem(IDC_STATIC7).EnableWindow(bEnable && m_SetsPP.bVPScaling);
		GetDlgItem(IDC_COMBO8).EnableWindow(bEnable && m_SetsPP.bVPScaling);
#ifdef _WIN64
		GetDlgItem(IDC_CHECK19).EnableWindow(bEnable && (m_SetsPP.bHdrPassthrough || m_SetsPP.bHdrLocalToneMapping));
#endif
	}

	GetDlgItem(IDC_STATIC_SDRTM).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_COMBO_SDRTM).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_STATIC8).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_EDIT1).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_SLIDER2).EnableWindow(m_SetsPP.bConvertToSdr);
	GetDlgItem(IDC_EDIT_DISPLAYMAX).EnableWindow(m_SetsPP.bHdrLocalToneMapping);
	const BOOL bLocal = m_SetsPP.bUseD3D11 && m_SetsPP.bHdrLocalToneMapping && IsWindows10OrGreater();
	GetDlgItem(IDC_STATIC_DVTM).EnableWindow(bLocal);
	GetDlgItem(IDC_COMBO_DVTM).EnableWindow(bLocal);
	GetDlgItem(IDC_CHECK20).EnableWindow(bLocal);
}

HRESULT CVRMainPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRMainPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	if (m_SetsPP.iSDRDisplayNits != m_oldSDRDisplayNits) {
		// OK or Apply buttons were not pressed. cancel the settings.
		m_pVideoRenderer->GetSettings(m_SetsPP);
		m_SetsPP.iSDRDisplayNits = m_oldSDRDisplayNits;
		m_pVideoRenderer->SetSettings(m_SetsPP);
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HRESULT CVRMainPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;
	m_neuralSettings = neural::Settings::Load();
    for (auto method : {neural::MotionMethod::Fast, neural::MotionMethod::EchoStorm})
        ComboBox_AddStringData(m_hWnd, IDC_MOTION_METHOD, neural::Name(method), static_cast<unsigned>(method));
	SendDlgItemMessageW(IDC_UPSCALER, CB_RESETCONTENT, 0, 0);
	for (auto provider : {processing::Upscaler::Off, processing::Upscaler::Dlss, processing::Upscaler::Fsr, processing::Upscaler::Xess})
		ComboBox_AddStringData(m_hWnd, IDC_UPSCALER, processing::Name(provider), static_cast<unsigned>(provider));
	SendDlgItemMessageW(IDC_NR_STAGE, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_NR_STAGE, L"Before upscaling", static_cast<unsigned>(neural::NrStage::BeforeUpscale));
	ComboBox_AddStringData(m_hWnd, IDC_NR_STAGE, L"After upscaling", static_cast<unsigned>(neural::NrStage::AfterUpscale));
	ComboBox_AddStringData(m_hWnd, IDC_NR_STAGE, L"Before and after", static_cast<unsigned>(neural::NrStage::Both));
	SendDlgItemMessageW(IDC_FRAME_GENERATOR, CB_RESETCONTENT, 0, 0);
	for (auto provider : {processing::FrameGenerator::EchoStorm, processing::FrameGenerator::Dlss, processing::FrameGenerator::Fsr, processing::FrameGenerator::Xess})
		ComboBox_AddStringData(m_hWnd, IDC_FRAME_GENERATOR, processing::Name(provider), static_cast<unsigned>(provider));
	SendDlgItemMessageW(IDC_DLSS_MFG, CB_RESETCONTENT, 0, 0);
	ComboBox_AddStringData(m_hWnd, IDC_DLSS_MFG, L"Off", 1);
	for (int multiplier : {2,3,4})
		ComboBox_AddStringData(m_hWnd, IDC_DLSS_MFG, (std::to_wstring(multiplier)+L"x").c_str(), multiplier);
	SendDlgItemMessageW(IDC_NEURAL_INTENSITY, CB_RESETCONTENT, 0, 0);
	for (int intensity : {0, 25, 50, 75, 100, 125, 150, 175, 200}) {
		ComboBox_AddStringData(m_hWnd, IDC_NEURAL_INTENSITY, (std::to_wstring(intensity) + L"%").c_str(), intensity);
	}


	m_pVideoRenderer->GetSettings(m_SetsPP);
	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;

	if (!IsWindows7SP1OrGreater()) {
		GetDlgItem(IDC_CHECK1).EnableWindow(FALSE);
		m_SetsPP.bUseD3D11 = false;
	}
	if (!IsWindows10OrGreater()) {
		GetDlgItem(IDC_COMBO10).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC5).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO7).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC6).EnableWindow(FALSE);
		GetDlgItem(IDC_SLIDER1).EnableWindow(FALSE);
		GetDlgItem(IDC_STATIC7).EnableWindow(FALSE);
		GetDlgItem(IDC_COMBO8).EnableWindow(FALSE);
		GetDlgItem(IDC_CHECK19).EnableWindow(FALSE);
	}

#ifndef _WIN64
	GetDlgItem(IDC_STATIC7).EnableWindow(FALSE);
	GetDlgItem(IDC_COMBO8).EnableWindow(FALSE);
	GetDlgItem(IDC_CHECK19).EnableWindow(FALSE);
#endif

	EnableControls();

	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Fixed font size");
	SendDlgItemMessageW(IDC_COMBO6, CB_ADDSTRING, 0, (LPARAM)L"Increase font by window");

	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"Auto 8/10-bit Integer",  0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"8-bit Integer",          8);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"10-bit Integer",        10);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO1, L"16-bit Floating Point", 16);

	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"Disable");
	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"Enable");
	SendDlgItemMessageW(IDC_COMBO9, CB_ADDSTRING, 0, (LPARAM)L"HACK future frames");

	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"Disable");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for SD");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 720p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1080p");
	SendDlgItemMessageW(IDC_COMBO8, CB_ADDSTRING, 0, (LPARAM)L"for \x2264 1440p");

	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Do not change");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off (fullscreen)");
	SendDlgItemMessageW(IDC_COMBO7, CB_ADDSTRING, 0, (LPARAM)L"Allow turn on/off");

	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO5, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");

	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Nearest-neighbor");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Mitchell-Netravali");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Catmull-Rom");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos2");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Lanczos3");
	SendDlgItemMessageW(IDC_COMBO2, CB_ADDSTRING, 0, (LPARAM)L"Jinc2m");

	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Box");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bilinear");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Hamming");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Bicubic sharp");
	SendDlgItemMessageW(IDC_COMBO3, CB_ADDSTRING, 0, (LPARAM)L"Lanczos");

	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Discard");
	SendDlgItemMessageW(IDC_COMBO4, CB_ADDSTRING, 0, (LPARAM)L"Flip");

	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETRANGE, 0, MAKELONG(0, 2));
	SendDlgItemMessageW(IDC_SLIDER1, TBM_SETTIC, 0, 1);

	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETRANGE, 0, MAKELONG(SDR_NITS_MIN / SDR_NITS_STEP, SDR_NITS_MAX / SDR_NITS_STEP));
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETTIC, 0, SDR_NITS_DEF / SDR_NITS_STEP);
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETLINESIZE, 0, 1); // arrow keys
	SendDlgItemMessageW(IDC_SLIDER2, TBM_SETPAGESIZE, 0, 5); // clicks on trackbar's channel

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Ignore", -1);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Passthrough", 0);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"ACES", 1);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Reinhard", 2);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Hable", 3);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Mobius", 4);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"BT2390", 5);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO10, L"Spline", 7);

	ComboBox_AddStringData(m_hWnd, IDC_COMBO_DVTM, L"ST 2094-10", 6);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO_DVTM, L"Same as HDR", 0);

	ComboBox_AddStringData(m_hWnd, IDC_COMBO_SDRTM, L"Fixed", SDRTM_Fixed);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO_SDRTM, L"Metadata", SDRTM_Metadata);
	ComboBox_AddStringData(m_hWnd, IDC_COMBO_SDRTM, L"Measured", SDRTM_Measured);

	SetControls();

	SetCursor(m_hWnd, IDC_ARROW);
	SetCursor(m_hWnd, IDC_COMBO1, IDC_HAND);

	AddHint(IDC_CHECK5,
		L"It works fast, but it's not always good.\n"
		"Disable it if you want to use shaders for resizing.");
	AddHint(IDC_COMBO8,
		L"Available for Direct3D 11.\n"
		"Requires hardware and driver support:\n"
		"- Intel Graphics UHD 610 or later\n"
		"- Nvidia RTX (x64 only)");
	AddHint(IDC_CHECK19,
		L"Available for Direct3D 11.\n"
		"Requires hardware and driver support:\n"
		"- Nvidia RTX (x64 only)");
	AddHint(IDC_COMBO10,
		L"Available for Direct3D 11.\n"
		"Applies to HDR10, HLG and Dolby Vision.\n"
		"Passthrough sends the video to the display as it is.\n"
		"A method tone maps it to the nits set on the right.\n"
		"Ignore leaves it to the settings below.");
	AddHint(IDC_COMBO_DVTM,
		L"Tone mapping method for Dolby Vision\n"
		"with dynamic metadata.\n"
		"Same as HDR uses the method above.");
	AddHint(IDC_CHECK20,
		L"Available for Direct3D 11.\n"
		"Tone map from the measured peak of each frame\n"
		"instead of the peak in the metadata.");
	AddHint(IDC_COMBO5,
		L"Used for YUV 4:2:0/4:2:2 input formats\n"
		"when the DVXA2/D3D11 Video Processor is not active.");
	AddHint(IDC_COMBO2,
		L"Used to increase image size when the\n"
		"DVXA2/D3D11 Video Processor is not used for resizing.");
	AddHint(IDC_COMBO3,
		L"Used to reduce image size when the\n"
		"DVXA2/D3D11 Video Processor is not used for resizing.");
	AddHint(IDC_COMBO4,
		L"'Flip' is more efficient, but 'Discard' may work\n"
		"more correctly in some rare situations.");

	m_bActivated = true;

	return S_OK;
}

INT_PTR CVRMainPPage::OnReceiveMessage(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND) {
		LRESULT lValue;
		const int nID = LOWORD(wParam);
		int action = HIWORD(wParam);

		if (action == BN_CLICKED) {
            if (nID == IDC_SHADER_PRE_ADD || nID == IDC_SHADER_PRE_CLEAR ||
                nID == IDC_SHADER_POST_ADD || nID == IDC_SHADER_POST_CLEAR) {
                EditShaderStage(nID == IDC_SHADER_PRE_ADD || nID == IDC_SHADER_PRE_CLEAR,
                    nID == IDC_SHADER_PRE_CLEAR || nID == IDC_SHADER_POST_CLEAR);
                return (LRESULT)1;
            }
			if (nID == IDC_AMD_BRIDGE) {
				m_neuralSettings.amdBridge = IsDlgButtonChecked(IDC_AMD_BRIDGE) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_MFG_PRESENT_THREAD) {
				m_neuralSettings.presentThread = IsDlgButtonChecked(IDC_MFG_PRESENT_THREAD) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_FG_LIMIT_DISPLAY) {
				m_neuralSettings.limitGenerationToDisplay = IsDlgButtonChecked(IDC_FG_LIMIT_DISPLAY) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
            if (nID == IDC_DLSS_SAVE_SAMPLE) {
                framegen::DlssSampleCapture::Request();
                return (LRESULT)1;
            }
            if (nID == IDC_DLSS_FG_GUARD) {
                m_neuralSettings.dlssFgGuard=IsDlgButtonChecked(IDC_DLSS_FG_GUARD)==BST_CHECKED;
                SetDirty(); return (LRESULT)1;
            }
			if (nID == IDC_ECHO_PIPELINE) {
				m_neuralSettings.echoPipeline = IsDlgButtonChecked(IDC_ECHO_PIPELINE) == BST_CHECKED;
				EnableControls(); SetDirty(); return (LRESULT)1;
			}
			if (nID == IDC_NEURAL_ENABLED) {
				m_neuralSettings.enabled = IsDlgButtonChecked(IDC_NEURAL_ENABLED) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_CHECK1) {
				m_SetsPP.bUseD3D11 = IsDlgButtonChecked(IDC_CHECK1) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK2) {
				m_SetsPP.bShowStats = IsDlgButtonChecked(IDC_CHECK2) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK3) {
				m_SetsPP.bDeintDouble = IsDlgButtonChecked(IDC_CHECK3) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK5) {
				m_SetsPP.bVPScaling = IsDlgButtonChecked(IDC_CHECK5) == BST_CHECKED;
				SetDirty();
				GetDlgItem(IDC_STATIC7).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				GetDlgItem(IDC_COMBO8).EnableWindow(m_SetsPP.bVPScaling && m_SetsPP.bUseD3D11 && IsWindows10OrGreater());
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK6) {
				m_SetsPP.bInterpolateAt50pct = IsDlgButtonChecked(IDC_CHECK6) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK7) {
				m_SetsPP.VPFmts.bNV12 = IsDlgButtonChecked(IDC_CHECK7) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK8) {
				m_SetsPP.VPFmts.bP01x = IsDlgButtonChecked(IDC_CHECK8) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK9) {
				m_SetsPP.VPFmts.bYUY2 = IsDlgButtonChecked(IDC_CHECK9) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK4) {
				m_SetsPP.VPFmts.bOther = IsDlgButtonChecked(IDC_CHECK4) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK10) {
				m_SetsPP.bUseDither = IsDlgButtonChecked(IDC_CHECK10) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK17) {
				m_SetsPP.bDeintBlend = IsDlgButtonChecked(IDC_CHECK17) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK11) {
				m_SetsPP.bExclusiveFS = IsDlgButtonChecked(IDC_CHECK11) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK15) {
				m_SetsPP.bVBlankBeforePresent = IsDlgButtonChecked(IDC_CHECK15) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK13) {
				m_SetsPP.bAdjustPresentTime = IsDlgButtonChecked(IDC_CHECK13) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK16) {
				m_SetsPP.bReinitByDisplay = IsDlgButtonChecked(IDC_CHECK16) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK18) {
				m_SetsPP.bHdrPreferDoVi = IsDlgButtonChecked(IDC_CHECK18) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK14) {
				m_SetsPP.bConvertToSdr = IsDlgButtonChecked(IDC_CHECK14) == BST_CHECKED;
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
			if (nID == IDC_CHECK20) {
				m_SetsPP.bHdrLocalMeasurePeak = IsDlgButtonChecked(IDC_CHECK20) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_CHECK19) {
				m_SetsPP.bVPRTXVideoHDR = IsDlgButtonChecked(IDC_CHECK19) == BST_CHECKED;
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_BUTTON1) {
				m_SetsPP.SetDefault();
				m_neuralSettings = {};
				SetControls();
				EnableControls();
				SetDirty();
				return (LRESULT)1;
			}
		}

		if (action == CBN_SELCHANGE) {
            if (nID == IDC_MOTION_METHOD) {
                m_neuralSettings.motionMethod = static_cast<neural::MotionMethod>(ComboBox_GetCurItemData(m_hWnd, IDC_MOTION_METHOD));
                SetDirty(); return (LRESULT)1;
            }
			if (nID == IDC_NR_STAGE) {
				m_neuralSettings.nrStage = static_cast<neural::NrStage>(ComboBox_GetCurItemData(m_hWnd, IDC_NR_STAGE));
				SetDirty(); return (LRESULT)1;
			}
			if (nID == IDC_UPSCALER) {
				m_neuralSettings.upscaler = static_cast<processing::Upscaler>(ComboBox_GetCurItemData(m_hWnd, IDC_UPSCALER));
				SetDirty(); return (LRESULT)1;
			}
			if (nID == IDC_FRAME_GENERATOR) {
				m_neuralSettings.frameGenerator = static_cast<processing::FrameGenerator>(ComboBox_GetCurItemData(m_hWnd, IDC_FRAME_GENERATOR));
				EnableControls(); SetDirty(); return (LRESULT)1;
			}
			if (nID == IDC_DLSS_MFG) {
				m_neuralSettings.frameMultiplier = static_cast<unsigned>(ComboBox_GetCurItemData(m_hWnd, IDC_DLSS_MFG));
				EnableControls(); SetDirty(); return (LRESULT)1;
			}
			if (nID == IDC_NEURAL_INTENSITY) {
				m_neuralSettings.intensity = static_cast<int>(ComboBox_GetCurItemData(m_hWnd, IDC_NEURAL_INTENSITY));
				SetDirty();
				return (LRESULT)1;
			}

			if (nID == IDC_COMBO6) {
				lValue = SendDlgItemMessageW(IDC_COMBO6, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iResizeStats) {
					m_SetsPP.iResizeStats = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO1) {
				lValue = ComboBox_GetCurItemData(m_hWnd, IDC_COMBO1);
				if (lValue != m_SetsPP.iTexFormat) {
					m_SetsPP.iTexFormat = lValue;
					SetDirty();
#ifdef _WIN64
					GetDlgItem(IDC_CHECK19).EnableWindow(m_SetsPP.bUseD3D11 && (m_SetsPP.bHdrPassthrough || m_SetsPP.bHdrLocalToneMapping) && m_SetsPP.iTexFormat != TEXFMT_8INT);
#endif
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO9) {
				lValue = SendDlgItemMessageW(IDC_COMBO9, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iVPDeinterlacing) {
					m_SetsPP.iVPDeinterlacing = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO8) {
				lValue = SendDlgItemMessageW(IDC_COMBO8, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iVPSuperRes) {
					m_SetsPP.iVPSuperRes = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO7) {
				lValue = SendDlgItemMessageW(IDC_COMBO7, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iHdrToggleDisplay) {
					m_SetsPP.iHdrToggleDisplay = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO5) {
				lValue = SendDlgItemMessageW(IDC_COMBO5, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iChromaScaling) {
					m_SetsPP.iChromaScaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO2) {
				lValue = SendDlgItemMessageW(IDC_COMBO2, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iUpscaling) {
					m_SetsPP.iUpscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO3) {
				lValue = SendDlgItemMessageW(IDC_COMBO3, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iDownscaling) {
					m_SetsPP.iDownscaling = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO4) {
				lValue = SendDlgItemMessageW(IDC_COMBO4, CB_GETCURSEL, 0, 0);
				if (lValue != m_SetsPP.iSwapEffect) {
					m_SetsPP.iSwapEffect = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO_DVTM) {
				lValue = ComboBox_GetCurItemData(m_hWnd, IDC_COMBO_DVTM);
				if (lValue != m_SetsPP.iHdrDoviToneMappingType) {
					m_SetsPP.iHdrDoviToneMappingType = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO_SDRTM) {
				lValue = ComboBox_GetCurItemData(m_hWnd, IDC_COMBO_SDRTM);
				if (lValue != m_SetsPP.iSdrToneMappingType) {
					m_SetsPP.iSdrToneMappingType = lValue;
					SetDirty();
				}
				return (LRESULT)1;
			}
			if (nID == IDC_COMBO10) {
				lValue = SendDlgItemMessageW(IDC_COMBO10, CB_GETCURSEL, 0, 0);
				switch (lValue) {
					case 0:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = false;
						break;
					case 1:
						m_SetsPP.bHdrPassthrough = true;
						m_SetsPP.bHdrLocalToneMapping = false;
						break;
					case 2:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 1;
						break;
					case 3:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 2;
						break;
					case 4:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 3;
						break;
					case 5:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 4;
						break;
					case 6:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 5;
						break;
					case 7:
						m_SetsPP.bHdrPassthrough = false;
						m_SetsPP.bHdrLocalToneMapping = true;
						m_SetsPP.iHdrLocalToneMappingType = 7;
						break;
					default:
						break;
				}
				SetDirty();
				EnableControls();
				return (LRESULT)1;
			}
		}
		if (action == EN_CHANGE) {
			if (nID == IDC_EDIT_DISPLAYMAX) {
				SetDirty();
			}
		}
	}
	else if (uMsg == WM_HSCROLL) {
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER1)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER1, TBM_GETPOS, 0, 0);
			if (lValue != m_SetsPP.iHdrOsdBrightness) {
				m_SetsPP.iHdrOsdBrightness = lValue;
				SetDirty();
			}
			return (LRESULT)1;
		}
		if ((HWND)lParam == GetDlgItem(IDC_SLIDER2)) {
			LRESULT lValue = SendDlgItemMessageW(IDC_SLIDER2, TBM_GETPOS, 0, 0);
			lValue *= SDR_NITS_STEP;
			if (lValue != m_SetsPP.iSDRDisplayNits) {
				m_SetsPP.iSDRDisplayNits = lValue;
				GetDlgItem(IDC_EDIT1).SetWindowTextW(std::to_wstring(m_SetsPP.iSDRDisplayNits).c_str());
				SetDirty();
				{
					// apply only SDRDisplayNits
					Settings_t sets;
					m_pVideoRenderer->GetSettings(sets);
					sets.iSDRDisplayNits = m_SetsPP.iSDRDisplayNits;
					m_pVideoRenderer->SetSettings(sets);
				}
			}
			return (LRESULT)1;
		}
	}

	// Let the parent class handle the message.
	return CBasePropertyPage::OnReceiveMessage(hwnd, uMsg, wParam, lParam);
}

void CVRMainPPage::EditShaderStage(bool before, bool clear)
{
    CComQIPtr<IExFilterConfig> config(m_pVideoRenderer);
    if (!config) return;
    HRESULT hr = S_OK;
    if (clear) {
        hr = config->Flt_SetBool(before ? "cmd_clearPreScaleShaders" : "cmd_clearPostScaleShaders", true);
    } else {
        CComPtr<IFileOpenDialog> dialog;
        hr = dialog.CoCreateInstance(__uuidof(FileOpenDialog));
        if (FAILED(hr)) return;
        const COMDLG_FILTERSPEC filter[] = {{L"HLSL pixel shader", L"*.hlsl;*.txt"}};
        dialog->SetFileTypes(1, filter);
        dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);
        dialog->SetTitle(before ? L"Add pre-resize shader (applies immediately)" : L"Add post-resize shader (applies immediately)");
        if (FAILED(dialog->Show(m_hWnd))) return;
        CComPtr<IShellItem> item;
        hr = dialog->GetResult(&item);
        if (FAILED(hr)) return;
        PWSTR path = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
        if (FAILED(hr)) return;
        const std::wstring name(path);
        CoTaskMemFree(path);
        HANDLE file = CreateFileW(name.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            MessageBoxW(L"Could not read the shader file.", L"Pixel shaders", MB_OK | MB_ICONERROR);
            return;
        }
        LARGE_INTEGER size{};
        std::string source;
        DWORD read = 0;
        if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 4 * 1024 * 1024) {
            source.resize(static_cast<size_t>(size.QuadPart));
            if (!ReadFile(file, source.data(), static_cast<DWORD>(source.size()), &read, nullptr) || read != source.size())
                source.clear();
        }
        CloseHandle(file);
        if (source.empty()) {
            MessageBoxW(L"The shader must be a non-empty UTF-8 HLSL file, at most 4 MB.", L"Pixel shaders", MB_OK | MB_ICONERROR);
            return;
        }
        if (source.starts_with("\xEF\xBB\xBF")) source.erase(0, 3);
        std::vector<BYTE> packet;
        auto append = [&](uint32_t code, const void* data, uint32_t length) {
            const auto offset = packet.size();
            packet.resize(offset + 8 + length);
            memcpy(packet.data() + offset, &code, 4);
            memcpy(packet.data() + offset + 4, &length, 4);
            memcpy(packet.data() + offset + 8, data, length);
        };
        append(FCC('NAME'), name.data(), static_cast<uint32_t>(name.size() * sizeof(wchar_t)));
        append(FCC('CODE'), source.data(), static_cast<uint32_t>(source.size()));
        hr = config->Flt_SetBin(before ? "cmd_addPreScaleShader" : "cmd_addPostScaleShader", packet.data(), static_cast<int>(packet.size()));
    }
    if (FAILED(hr)) {
        MessageBoxW(L"Could not apply the shader. Open a video first and use standalone HLSL with a main entry point (ps_4_0).",
            L"Pixel shaders", MB_OK | MB_ICONERROR);
    } else config->Flt_SetBool("cmd_redraw", true);
}

HRESULT CVRMainPPage::OnApplyChanges()
{
	const auto error = m_neuralSettings.Save();
	if (error != ERROR_SUCCESS) return HRESULT_FROM_WIN32(error);

	BOOL translated = FALSE;
	int displayMaxNits = GetDlgItemInt(IDC_EDIT_DISPLAYMAX, &translated, FALSE);
	if (!translated) {
		MessageBoxW(L"Invalid HDR Brightness. Please enter a valid number from 100 to 10000.", L"Error", MB_OK | MB_ICONERROR);
	}
	else if (displayMaxNits <= HDR_NITS_MIN || displayMaxNits > HDR_NITS_MAX) {
		MessageBoxW(L"Invalid HDR Brightness. Please enter a valid number from 100 to 10000.", L"Error", MB_OK | MB_ICONERROR);
	}
	else {
		m_SetsPP.iHdrDisplayMaxNits = displayMaxNits;
	}

	m_pVideoRenderer->SetSettings(m_SetsPP);
	m_pVideoRenderer->SaveSettings();

	m_oldSDRDisplayNits = m_SetsPP.iSDRDisplayNits;

	return S_OK;
}

HWND CVRMainPPage::CreateHintWindow(HWND parent, int timePop, int timeInit, int timeReshow)
{
	HWND hhint = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASS, nullptr,
		WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, parent, nullptr, nullptr, nullptr);

	SetWindowPos(hhint, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_AUTOPOP, MAKELONG(timePop, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_INITIAL, MAKELONG(timeInit, 0));
	SendMessageW(hhint, TTM_SETDELAYTIME, TTDT_RESHOW, MAKELONG(timeReshow, 0));
	SendMessageW(hhint, TTM_SETMAXTIPWIDTH, 0, 470);
	return hhint;
}

void CVRMainPPage::AddHint(int id, const LPCWSTR text)
{
	if (!m_hHint) {
		m_hHint = CreateHintWindow(m_Dlg, 15000);
	}
	TOOLINFOW ti;
	ti.cbSize = sizeof(TOOLINFOW);
	ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND;
	ti.hwnd = m_Dlg;
	ti.uId = (LPARAM)GetDlgItem(id).m_hWnd;
	ti.lpszText = const_cast<LPWSTR>(text);
	SendMessageW(m_hHint, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}

// CVRInfoPPage

CVRInfoPPage::CVRInfoPPage(LPUNKNOWN lpunk, HRESULT* phr) :
	CBasePropertyPage(L"InfoProp", lpunk, IDD_INFOPROPPAGE, IDS_INFOPROPPAGE_TITLE)
{
	DLog(L"CVRInfoPPage()");
}

CVRInfoPPage::~CVRInfoPPage()
{
	DLog(L"~CVRInfoPPage()");

	if (m_hMonoFont) {
		DeleteObject(m_hMonoFont);
		m_hMonoFont = 0;
	}
}

HRESULT CVRInfoPPage::OnConnect(IUnknown *pUnk)
{
	if (pUnk == nullptr) return E_POINTER;

	m_pVideoRenderer = pUnk;
	if (!m_pVideoRenderer) {
		return E_NOINTERFACE;
	}

	return S_OK;
}

HRESULT CVRInfoPPage::OnDisconnect()
{
	if (m_pVideoRenderer == nullptr) {
		return E_UNEXPECTED;
	}

	m_pVideoRenderer.Release();

	return S_OK;
}

HWND GetParentOwner(HWND hwnd)
{
	HWND hWndParent = hwnd;
	HWND hWndT;
	while ((::GetWindowLongPtrW(hWndParent, GWL_STYLE) & WS_CHILD) &&
		(hWndT = ::GetParent(hWndParent)) != NULL) {
		hWndParent = hWndT;
	}

	return hWndParent;
}

static WNDPROC OldControlProc;
static LRESULT CALLBACK ControlProc(HWND control, UINT message, WPARAM wParam, LPARAM lParam)
{
	if (message == WM_KEYDOWN && LOWORD(wParam) == VK_ESCAPE) {
		// fixed Esc handling when EDITTEXT control has ES_MULTILINE property and is in focus
		HWND parentOwner = GetParentOwner(control);
		if (parentOwner) {
			::PostMessageW(parentOwner, WM_COMMAND, IDCANCEL, 0);
		}
		return TRUE;
	}

	return CallWindowProcW(OldControlProc, control, message, wParam, lParam); // call edit control's own windowproc
}

HRESULT CVRInfoPPage::OnActivate()
{
	// set m_hWnd for CWindow
	m_hWnd = m_hwnd;

	SetDlgItemTextW(IDC_EDIT2, GetNameAndVersion());

	// init monospace font
	LOGFONTW lf = {};
	HDC hdc = GetWindowDC();
	lf.lfHeight = -MulDiv(9, GetDeviceCaps(hdc, LOGPIXELSY), 72);
	ReleaseDC(hdc);
	lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
	wcscpy_s(lf.lfFaceName, L"Consolas");
	m_hMonoFont = CreateFontIndirectW(&lf);

	GetDlgItem(IDC_EDIT1).SetFont(m_hMonoFont);
	ASSERT(m_pVideoRenderer);

	if (!m_pVideoRenderer->GetActive()) {
		SetDlgItemTextW(IDC_EDIT1, L"filter is not active");
		return S_OK;
	}

	std::wstring strInfo(L"Windows ");
	strInfo.append(GetWindowsVersion());
	strInfo.append(L"\r\n");

	std::wstring strVP;
	if (S_OK == m_pVideoRenderer->GetVideoProcessorInfo(strVP)) {
		str_replace(strVP, L"\n", L"\r\n");
		strInfo.append(strVP);
	}

#ifdef _DEBUG
	{
		std::vector<DisplayConfig_t> displayConfigs;

		bool ret = GetDisplayConfigs(displayConfigs);

		strInfo.append(L"\r\n");

		for (const auto& dc : displayConfigs) {
			double freq = (double)dc.refreshRate.Numerator / (double)dc.refreshRate.Denominator;
			strInfo += std::format(L"\r\n{} - {:.3f} Hz", dc.displayName, freq);

			if (dc.bitsPerChannel) { // if bitsPerChannel is not set then colorEncoding and other values are invalid
				const wchar_t* colenc = ColorEncodingToString(dc.colorEncoding);
				if (colenc) {
					strInfo += std::format(L" {}", colenc);
				}
				strInfo += std::format(L" {}-bit", dc.bitsPerChannel);
			}

			const wchar_t* output = OutputTechnologyToString(dc.outputTechnology);
			if (output) {
				strInfo += std::format(L" {}", output);
			}
		}
	}
#endif

	SetDlgItemTextW(IDC_EDIT1, strInfo.c_str());

	OldControlProc = (WNDPROC)::SetWindowLongPtrW(::GetDlgItem(m_hWnd, IDC_EDIT1), GWLP_WNDPROC, (LONG_PTR)ControlProc);

	return S_OK;
}
