/*
 * Copyright 2014, Stephan Aßmus <superstippi@gmx.de>.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "UserLoginWindow.h"

#include <algorithm>
#include <stdio.h>

#include <mail_encoding.h>

#include <Alert.h>
#include <Autolock.h>
#include <Catalog.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <TabView.h>
#include <TextControl.h>

#include "BitmapView.h"
#include "Model.h"
#include "WebAppInterface.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "UserLoginWindow"


enum {
	MSG_SEND					= 'send',
	MSG_TAB_SELECTED			= 'tbsl',
	MSG_CAPTCHA_OBTAINED		= 'cpob',
};


class TabView : public BTabView {
public:
	TabView(const BMessenger& target, const BMessage& message)
		:
		BTabView("tab view", B_WIDTH_FROM_WIDEST),
		fTarget(target),
		fMessage(message)
	{
	}

	virtual void Select(int32 tabIndex)
	{
		BTabView::Select(tabIndex);

		BMessage message(fMessage);
		message.AddInt32("tab index", tabIndex);
		fTarget.SendMessage(&message);
	}

private:
	BMessenger	fTarget;
	BMessage	fMessage;
};


UserLoginWindow::UserLoginWindow(BWindow* parent, BRect frame, Model& model)
	:
	BWindow(frame, B_TRANSLATE_SYSTEM_NAME("Log in"),
		B_FLOATING_WINDOW_LOOK, B_FLOATING_SUBSET_WINDOW_FEEL,
		B_ASYNCHRONOUS_CONTROLS | B_AUTO_UPDATE_SIZE_LIMITS
			| B_NOT_RESIZABLE | B_NOT_ZOOMABLE),
	fModel(model),
	fMode(NONE),
	fWorkerThread(-1)
{
	AddToSubset(parent);

	fUsernameField = new BTextControl(B_TRANSLATE("User name:"), "", NULL);
	fPasswordField = new BTextControl(B_TRANSLATE("Pass phrase:"), "", NULL);

	fNewUsernameField = new BTextControl(B_TRANSLATE("User name:"), "", NULL);
	fNewPasswordField = new BTextControl(B_TRANSLATE("Pass phrase:"), "",
		NULL);
	fRepeatPasswordField = new BTextControl(B_TRANSLATE("Repeat pass phrase:"),
		"", NULL);
	fLanguageCodeField = new BTextControl(B_TRANSLATE("Language code:"),
		model.PreferredLanguage(), NULL);
	fEmailField = new BTextControl(B_TRANSLATE("Email address:"), "", NULL);
	fCaptchaView = new BitmapView("captcha view");
	fCaptchaResultField = new BTextControl("", "", NULL);

	fTabView = new TabView(BMessenger(this),
		BMessage(MSG_TAB_SELECTED));

	BGridView* loginCard = new BGridView(B_TRANSLATE("Log in"));
	BLayoutBuilder::Grid<>(loginCard)
		.AddTextControl(fUsernameField, 0, 0)
		.AddTextControl(fPasswordField, 0, 1)
		.AddGlue(0, 2)

		.SetInsets(B_USE_DEFAULT_SPACING)
	;
	fTabView->AddTab(loginCard);

	BGridView* createAccountCard = new BGridView(B_TRANSLATE("Create account"));
	BLayoutBuilder::Grid<>(createAccountCard)
		.AddTextControl(fNewUsernameField, 0, 0)
		.AddTextControl(fNewPasswordField, 0, 1)
		.AddTextControl(fRepeatPasswordField, 0, 2)
		.AddTextControl(fEmailField, 0, 3)
		.AddTextControl(fLanguageCodeField, 0, 4)
		.Add(fCaptchaView, 0, 5)
		.Add(fCaptchaResultField, 1, 5)

		.SetInsets(B_USE_DEFAULT_SPACING)
	;
	fTabView->AddTab(createAccountCard);

	fSendButton = new BButton("send", B_TRANSLATE("Log in"),
		new BMessage(MSG_SEND));
	fCancelButton = new BButton("cancel", B_TRANSLATE("Cancel"),
		new BMessage(B_QUIT_REQUESTED));

	// Build layout
	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.Add(fTabView)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(fCancelButton)
			.Add(fSendButton)
		.End()
		.SetInsets(B_USE_DEFAULT_SPACING)
	;

	SetDefaultButton(fSendButton);
	
	// TODO: Set initial mode based on whether there is account info
	_SetMode(LOGIN);

	CenterIn(parent->Frame());
}


UserLoginWindow::~UserLoginWindow()
{
	BAutolock locker(&fLock);
	
	if (fWorkerThread >= 0)
		wait_for_thread(fWorkerThread, NULL);
}


void
UserLoginWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MSG_SEND:
			switch (fMode) {
				case LOGIN:
					_Login();
					break;
				case CREATE_ACCOUNT:
					_CreateAccount();
					break;
				default:
					break;
			}
			break;

		case MSG_TAB_SELECTED:
		{
			int32 tabIndex;
			if (message->FindInt32("tab index", &tabIndex) == B_OK) {
				switch (tabIndex) {
					case 0:
						_SetMode(LOGIN);
						break;
					case 1:
						_SetMode(CREATE_ACCOUNT);
						break;
					default:
						break;
				}
			}
			break;
		}

		case MSG_CAPTCHA_OBTAINED:
			if (fCaptchaImage.Get() != NULL) {
				fCaptchaView->SetBitmap(
					fCaptchaImage->Bitmap(SharedBitmap::SIZE_ANY));
			} else {
				fCaptchaView->SetBitmap(NULL);
			}
			break;

		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
UserLoginWindow::_SetMode(Mode mode)
{
	if (fMode == mode)
		return;

	fMode = mode;

	switch (fMode) {
		case LOGIN:
			fTabView->Select((int32)0);
			fSendButton->SetLabel(B_TRANSLATE("Log in"));
			fUsernameField->MakeFocus();
			break;
		case CREATE_ACCOUNT:
			fTabView->Select(1);
			fSendButton->SetLabel(B_TRANSLATE("Create account"));
			if (fCaptchaToken.IsEmpty())
				_RequestCaptcha();
			fNewUsernameField->MakeFocus();
			break;
		default:
			break;
	}
}


void
UserLoginWindow::_Login()
{
	BAutolock locker(&fLock);
	
	if (fWorkerThread >= 0)
		return;

	thread_id thread = spawn_thread(&_AuthenticateThreadEntry,
		"Authenticator", B_NORMAL_PRIORITY, this);
	if (thread >= 0)
		_SetWorkerThread(thread);
}


void
UserLoginWindow::_CreateAccount()
{
	BAutolock locker(&fLock);
	
	if (fWorkerThread >= 0)
		return;

	thread_id thread = spawn_thread(&_CreateAccountThreadEntry,
		"Account creator", B_NORMAL_PRIORITY, this);
	if (thread >= 0)
		_SetWorkerThread(thread);
}


void
UserLoginWindow::_RequestCaptcha()
{
	if (Lock()) {
		fCaptchaToken = "";
		fCaptchaView->SetBitmap(NULL);
		fCaptchaImage.Unset();
		Unlock();
	}

	BAutolock locker(&fLock);
	
	if (fWorkerThread >= 0)
		return;

	thread_id thread = spawn_thread(&_RequestCaptchaThreadEntry,
		"Captcha requester", B_NORMAL_PRIORITY, this);
	if (thread >= 0)
		_SetWorkerThread(thread);
}


void
UserLoginWindow::_SetWorkerThread(thread_id thread)
{
	if (!Lock())
		return;
	
	bool enabled = thread < 0;

	fUsernameField->SetEnabled(enabled);
	fPasswordField->SetEnabled(enabled);
	fNewUsernameField->SetEnabled(enabled);
	fNewPasswordField->SetEnabled(enabled);
	fRepeatPasswordField->SetEnabled(enabled);
	fEmailField->SetEnabled(enabled);
	fLanguageCodeField->SetEnabled(enabled);
	fCaptchaResultField->SetEnabled(enabled);
	fSendButton->SetEnabled(enabled);
	
	if (thread >= 0) {
		fWorkerThread = thread;
		resume_thread(fWorkerThread);
	} else {
		fWorkerThread = -1;
	}

	Unlock();
}


int32
UserLoginWindow::_AuthenticateThreadEntry(void* data)
{
	UserLoginWindow* window = reinterpret_cast<UserLoginWindow*>(data);
	window->_AuthenticateThread();
	return 0;
}


void
UserLoginWindow::_AuthenticateThread()
{
	if (!Lock())
		return;

	BString nickName(fUsernameField->Text());
	BString passwordClear(fPasswordField->Text());

	Unlock();

	WebAppInterface interface;
	BMessage info;

	status_t status = interface.AuthenticateUser(
		nickName, passwordClear, info);

	BString error = B_TRANSLATE("Authentication failed. "
		"Connection to the service failed.");

	BMessage result;
	if (status == B_OK && info.FindMessage("result", &result) == B_OK) {
		BString token;
		if (result.FindString("token", &token) == B_OK && !token.IsEmpty()) {
			// We don't care for or store the token for now. The web-service
			// supports two methods of authorizing requests. One is via
			// Basic Authentication in the HTTP header, the other is via
			// Token Bearer. Since the connection is encrypted, it is hopefully
			// ok to send the password with each request instead of implementing
			// the Token Bearer. See section 5.1.2 in the haiku-depot-web
			// documentation.
			error = "";
			fModel.SetAuthorization(nickName, passwordClear, true);
		} else {
			error = B_TRANSLATE("Authentication failed. The user does "
				"not exist or the wrong password was supplied.");
		}
	}

	if (!error.IsEmpty()) {
		BAlert* alert = new(std::nothrow) BAlert(
			B_TRANSLATE("Authentication failed"),
			error,
			B_TRANSLATE("Close"));

		if (alert != NULL)
			alert->Go();

		_SetWorkerThread(-1);
	} else {
		_SetWorkerThread(-1);
		BMessenger(this).SendMessage(B_QUIT_REQUESTED);

		BAlert* alert = new(std::nothrow) BAlert(
			B_TRANSLATE("Success"),
			B_TRANSLATE("The authentication was successful."),
			B_TRANSLATE("Close"));

		if (alert != NULL)
			alert->Go();
	}
}


int32
UserLoginWindow::_RequestCaptchaThreadEntry(void* data)
{
	UserLoginWindow* window = reinterpret_cast<UserLoginWindow*>(data);
	window->_RequestCaptchaThread();
	return 0;
}


void
UserLoginWindow::_RequestCaptchaThread()
{
	WebAppInterface interface;
	BMessage info;

	status_t status = interface.RequestCaptcha(info);

	BAutolock locker(&fLock);

	BMessage result;
	if (status == B_OK && info.FindMessage("result", &result) == B_OK) {
		result.FindString("token", &fCaptchaToken);
		BString imageDataBase64;
		if (result.FindString("pngImageDataBase64", &imageDataBase64) == B_OK) {
			ssize_t encodedSize = imageDataBase64.Length();
			ssize_t decodedSize = (encodedSize * 3 + 3) / 4;
			if (decodedSize > 0) {
				char* buffer = new char[decodedSize];
				decodedSize = decode_base64(buffer, imageDataBase64.String(),
					encodedSize);
				if (decodedSize > 0) {
					BMemoryIO memoryIO(buffer, (size_t)decodedSize);
					fCaptchaImage.SetTo(new(std::nothrow) SharedBitmap(
						memoryIO), true);
					BMessenger(this).SendMessage(MSG_CAPTCHA_OBTAINED);
				} else {
					fprintf(stderr, "Failed to decode captcha: %s\n",
						strerror(decodedSize));
				}
				delete[] buffer;
			}
		}			
	} else {
		fprintf(stderr, "Failed to obtain captcha: %s\n", strerror(status));
	}

	_SetWorkerThread(-1);
}


int32
UserLoginWindow::_CreateAccountThreadEntry(void* data)
{
	UserLoginWindow* window = reinterpret_cast<UserLoginWindow*>(data);
	window->_CreateAccountThread();
	return 0;
}


void
UserLoginWindow::_CreateAccountThread()
{
	if (!Lock())
		return;

	BString nickName(fNewUsernameField->Text());
	BString passwordClear(fNewPasswordField->Text());
	BString email(fEmailField->Text());
	BString captchaToken(fCaptchaToken);
	BString captchaResponse(fCaptchaResultField->Text());
	BString languageCode(fLanguageCodeField->Text());

	Unlock();

	WebAppInterface interface;
	BMessage info;

	status_t status = interface.CreateUser(
		nickName, passwordClear, email, captchaToken, captchaResponse,
		languageCode, info);

	BAutolock locker(&fLock);

	BString error = B_TRANSLATE(
		"There was a puzzling response from the web service.");

	BMessage result;
	if (status == B_OK) {
		if (info.FindMessage("result", &result) == B_OK) {
			error = "";
		} else if (info.FindMessage("error", &result) == B_OK) {
			result.PrintToStream();
			BString message;
			if (result.FindString("message", &message) == B_OK) {
				if (message == "captchabadresponse") {
					error = B_TRANSLATE("You have not solved the captcha "
						"puzzle correctly.");
				} else if (message == "validationerror") {
					_CollectValidationFailures(result, error);
				} else {
					error << B_TRANSLATE("The web service responded with: ");
					error << message;
				}
			}
		}
	} else {
		error = B_TRANSLATE(
			"It was not possible to contact the web service.");
	}

	locker.Unlock();

	if (!error.IsEmpty()) {
		BAlert* alert = new(std::nothrow) BAlert(
			B_TRANSLATE("Failed to create account"),
			error,
			B_TRANSLATE("Close"));

		if (alert != NULL)
			alert->Go();

		fprintf(stderr,
			B_TRANSLATE("Failed to create account: %s\n"), error.String());

		_SetWorkerThread(-1);

		// We need a new captcha, it can be used only once
		fCaptchaToken = "";
		_RequestCaptcha();
	} else {
		fModel.SetAuthorization(nickName, passwordClear, true);

		_SetWorkerThread(-1);
		BMessenger(this).SendMessage(B_QUIT_REQUESTED);

		BAlert* alert = new(std::nothrow) BAlert(
			B_TRANSLATE("Success"),
			B_TRANSLATE("Account created successfully. "
				"You can now rate packages and do other useful things."),
			B_TRANSLATE("Close"));

		if (alert != NULL)
			alert->Go();
	}
}


void
UserLoginWindow::_CollectValidationFailures(const BMessage& result,
	BString& error) const
{
	error = B_TRANSLATE("There are problems with the data you entered:\n\n");

	bool found = false;

	BMessage data;
	BMessage failures;
	if (result.FindMessage("data", &data) == B_OK
		&& data.FindMessage("validationfailures", &failures) == B_OK) {
		int32 index = 0;
		while (true) {
			BString name;
			name << index++;
			BMessage failure;
			if (failures.FindMessage(name, &failure) != B_OK)
				break;
	
			BString property;
			BString message;
			if (failure.FindString("property", &property) == B_OK
				&& failure.FindString("message", &message) == B_OK) {
				found = true;
				if (property == "nickname" && message == "notunique") {
					error << B_TRANSLATE(
						"The username is already taken. "
						"Please choose another.");
				} else if (property == "passwordClear"
					&& message == "invalid") {
					error << B_TRANSLATE(
						"The password is too weak or invalid. "
						"Please use at least 8 characters with "
						"at least 2 numbers and 2 upper-case "
						"letters.");
				} else if (property == "email" && message == "malformed") {
					error << B_TRANSLATE(
						"The email address appears to be malformed.");
				} else {
					error << property << ": " << message;
				}
			}
		}
	}
	
	if (!found) {
		error << B_TRANSLATE("But none could be listed here, sorry.");
	}
}