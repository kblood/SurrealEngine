
#include "Precomp.h"
#include "UDXExtString.h"
#include "DXTextTokenizer.h"
#include "Utils/Logger.h"

void UDXExtString::Load(ObjectStream* stream)
{
	UObject::Load(stream);

	std::string text = stream->ReadString();
	if (!Text().empty())
		LogMessage("ExtString already has text content in its Text property!");
	Text() = text;
	stream->ThrowIfNotEnd();
}


void UDXExtString::AppendText(const std::string& newText)
{
	LogMessage("Usage check: ExtString.AppendText");
	Text() += newText;
}

int UDXExtString::GetFirstTextPart(std::string& outText)
{
	SpeechPage() = 0;
	return ReadDXTextPage(Text(), SpeechPage(), outText);
}

int UDXExtString::GetNextTextPart(std::string& outText)
{
	SpeechPage()++;
	return ReadDXTextPage(Text(), SpeechPage(), outText);
}

std::string& UDXExtString::GetText()
{
	LogMessage("Usage check: ExtString.GetText");
	return Text();
}

int UDXExtString::GetTextLength()
{
	LogMessage("Usage check: ExtString.GetTextLength");
	return (int)Text().length();
}

int UDXExtString::GetTextPart(int startPos, int count, std::string& outText)
{
	LogMessage("Usage check: ExtString.GetTextPart");
	outText = Text().substr(startPos, count);
	return (int)outText.length();
}

void UDXExtString::SetText(const std::string& newText)
{
	LogMessage("Usage check: ExtString.SetText");
	Text() = newText;
}
