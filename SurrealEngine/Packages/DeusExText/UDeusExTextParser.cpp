#include "Precomp.h"
#include "UDeusExTextParser.h"
#include "Packages/Extension/UExtString.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Engine.h"
#include "Utils/Logger.h"

bool UDXTextParser::OpenText(NameString textName, std::string textPackage)
{
	CloseText();
	if (textName.IsNone())
		textName = "DeusExQuotes"; // Not correct, but it handles empty strings just fine.
	if (textPackage.empty())
		textPackage = "DeusExText";
	textObject = UObject::Cast<UDXExtString>(engine->packages->GetPackage(textPackage)->GetUObject("ExtString", textName));
	Text() = textObject ? 1 : 0;
	return textObject;
}

void UDXTextParser::CloseText()
{
	textObject = nullptr;
	Text() = 0;
	TextPos() = 0;
	TagEndPos() = 0;
}

bool UDXTextParser::ProcessText()
{
	if (!textObject)
		return false;

	size_t position = TextPos() < 0 ? 0 : static_cast<size_t>(TextPos());
	DeusExTextToken token;
	if (!DeusExTextTokenizer::Next(textObject->Text(), position, PlayerName(), PlayerFirstName(), token))
	{
		TextPos() = static_cast<int>(position);
		return false;
	}

	TextPos() = static_cast<int>(position);
	TagEndPos() = static_cast<int>(position);
	LastTag() = token.Tag;
	LastText() = std::move(token.Text);
	LastName() = token.Name.empty() ? NameString() : NameString(token.Name);
	LastColor().R = token.Color.R;
	LastColor().G = token.Color.G;
	LastColor().B = token.Color.B;
	LastColor().A = token.Color.A;
	LastEmailName() = std::move(token.EmailName);
	LastEmailSubject() = std::move(token.EmailSubject);
	LastEmailFrom() = std::move(token.EmailFrom);
	LastEmailTo() = std::move(token.EmailTo);
	LastEmailCC() = std::move(token.EmailCC);
	LastFileName() = std::move(token.FileName);
	LastFileDescription() = std::move(token.FileDescription);
	return true;
}

bool UDXTextParser::IsEOF()
{
	return !textObject || TextPos() >= static_cast<int>(textObject->Text().size());
}

std::string UDXTextParser::GetText()
{
	return LastText();
}

void UDXTextParser::GotoLabel(const std::string& label)
{
	// The original DeusExText.dll implementation is also a no-op.
}

uint8_t UDXTextParser::GetTag()
{
	return static_cast<uint8_t>(LastTag());
}

NameString UDXTextParser::GetName()
{
	if (LastTag() == DeusExTextTags::TT_Note ||
		(LastTag() >= DeusExTextTags::TT_Graphic && LastTag() <= DeusExTextTags::TT_Label))
	{
		return LastName();
	}
	return {};
}

Color UDXTextParser::GetColor()
{
	if (LastTag() == DeusExTextTags::TT_DefaultColor || LastTag() == DeusExTextTags::TT_TextColor)
		return LastColor();
	if (LastTag() == DeusExTextTags::TT_RevertColor)
		return DefaultColor();
	return {};
}

void UDXTextParser::GetEmailInfo(std::string& name, std::string& subject, std::string& from, std::string& to, std::string& cc)
{
	name = "";
	subject = "";
	from = "";
	to = "";
	cc = "";
	if (LastTag() == DeusExTextTags::TT_Email)
	{
		name = LastEmailName();
		subject = LastEmailSubject();
		from = LastEmailFrom();
		to = LastEmailTo();
		cc = LastEmailCC();
	}
}

void UDXTextParser::GetFileInfo(std::string& fileName, std::string& fileDescription)
{
	fileName = "";
	fileDescription = "";
	if (LastTag() == DeusExTextTags::TT_File)
	{
		fileName = LastFileName();
		fileDescription = LastFileDescription();
	}
}

void UDXTextParser::SetPlayerName(const std::string& newPlayerName)
{
	PlayerName() = newPlayerName;
	const size_t separator = newPlayerName.find(' ');
	PlayerFirstName() = separator == std::string::npos ? newPlayerName : newPlayerName.substr(0, separator);
}
