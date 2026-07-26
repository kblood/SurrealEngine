#include "BotBenchmarkBuildIdentity.h"

#include "SurrealBuildIdentityGenerated.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#ifdef WIN32
#include <Windows.h>
#endif

namespace
{
	bool IsLowerHex40(const std::string& value)
	{
		return value.size() == 40 && std::all_of(value.begin(), value.end(), [](unsigned char c)
		{
			return std::isdigit(c) || (c >= 'a' && c <= 'f');
		});
	}

	std::string JsonString(const std::string& value)
	{
		std::ostringstream out;
		out << '"';
		for (unsigned char c : value)
		{
			switch (c)
			{
			case '\\': out << "\\\\"; break;
			case '"': out << "\\\""; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default: out << static_cast<char>(c); break;
			}
		}
		out << '"';
		return out.str();
	}

	class Sha256
	{
	public:
		void Add(const uint8_t* data, size_t count)
		{
			Total += count;
			while (count > 0)
			{
				const size_t capacity = Block.size() - Used;
				const size_t take = count < capacity ? count : capacity;
				std::copy_n(data, take, Block.data() + Used);
				Used += take; data += take; count -= take;
				if (Used == Block.size()) { Transform(Block.data()); Used = 0; }
			}
		}

		std::string Finish()
		{
			const uint64_t bits = Total * 8;
			const uint8_t one = 0x80;
			Add(&one, 1);
			const uint8_t zero = 0;
			while (Used != 56) Add(&zero, 1);
			std::array<uint8_t, 8> length{};
			for (int i = 0; i != 8; ++i) length[7 - i] = static_cast<uint8_t>(bits >> (i * 8));
			Add(length.data(), length.size());
			std::ostringstream out;
			out << std::uppercase << std::hex << std::setfill('0');
			for (uint32_t value : State)
				out << std::setw(8) << value;
			return out.str();
		}

	private:
		static uint32_t Rotate(uint32_t value, uint32_t bits)
		{
			return (value >> bits) | (value << (32 - bits));
		}
		void Transform(const uint8_t* bytes)
		{
			static constexpr std::array<uint32_t, 64> K = { 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
			std::array<uint32_t, 64> w{};
			for (size_t i = 0; i != 16; ++i)
				w[i] = (uint32_t(bytes[i * 4]) << 24) | (uint32_t(bytes[i * 4 + 1]) << 16) | (uint32_t(bytes[i * 4 + 2]) << 8) | bytes[i * 4 + 3];
			for (size_t i = 16; i != w.size(); ++i)
			{
				const uint32_t s0 = Rotate(w[i - 15], 7) ^ Rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
				const uint32_t s1 = Rotate(w[i - 2], 17) ^ Rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			uint32_t a=State[0], b=State[1], c=State[2], d=State[3], e=State[4], f=State[5], g=State[6], h=State[7];
			for (size_t i = 0; i != 64; ++i)
			{
				const uint32_t s1 = Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25);
				const uint32_t ch = (e & f) ^ (~e & g);
				const uint32_t t1 = h + s1 + ch + K[i] + w[i];
				const uint32_t s0 = Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22);
				const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
				h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+s0+maj;
			}
			State[0]+=a; State[1]+=b; State[2]+=c; State[3]+=d; State[4]+=e; State[5]+=f; State[6]+=g; State[7]+=h;
		}
		std::array<uint32_t, 8> State = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
		std::array<uint8_t, 64> Block{};
		size_t Used = 0;
		uint64_t Total = 0;
	};

	std::string HashFile(const std::filesystem::path& path, uint64_t& size, std::string& error)
	{
		std::ifstream input(path, std::ios::binary);
		if (!input) { error = "could not open executable for SHA-256"; return {}; }
		Sha256 hash;
		std::array<uint8_t, 64 * 1024> buffer{};
		size = 0;
		while (input)
		{
			input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
			const std::streamsize got = input.gcount();
			if (got > 0) { hash.Add(buffer.data(), static_cast<size_t>(got)); size += static_cast<uint64_t>(got); }
		}
		if (!input.eof()) { error = "could not read executable for SHA-256"; return {}; }
		if (size == 0) { error = "executable is empty"; return {}; }
		return hash.Finish();
	}
}

std::optional<BotBenchmarkBuildIdentity> BotBenchmarkBuildIdentity::TryCreate(std::string commit,
	std::string tree, bool dirty, const std::string& executablePath, std::string& error)
{
	if (!IsLowerHex40(commit) || !IsLowerHex40(tree)) { error = "build Git commit/tree are unavailable or invalid"; return {}; }
	const std::filesystem::path path(executablePath);
	if (path.filename().empty()) { error = "runtime executable path has no filename"; return {}; }
	BotBenchmarkBuildIdentity result;
	result.Commit = std::move(commit); result.Tree = std::move(tree); result.Dirty = dirty;
	result.ExecutableName = path.filename().string();
	result.ExecutableSha256 = HashFile(path, result.ExecutableSizeBytes, error);
	if (result.ExecutableSha256.empty()) return {};
	std::string canonical = "surreal-engine-build-identity-v1";
	for (const std::string* field : { &result.Commit, &result.Tree })
	{
		canonical.push_back('\0');
		canonical += *field;
	}
	canonical.push_back('\0');
	canonical += result.Dirty ? "true" : "false";
	canonical.push_back('\0');
	canonical += result.ExecutableName;
	canonical.push_back('\0');
	canonical += std::to_string(result.ExecutableSizeBytes);
	canonical.push_back('\0');
	canonical += result.ExecutableSha256;
	Sha256 hash; hash.Add(reinterpret_cast<const uint8_t*>(canonical.data()), canonical.size());
	result.IdentityId = "sha256:" + hash.Finish();
	return result;
}

std::optional<BotBenchmarkBuildIdentity> BotBenchmarkBuildIdentity::TryCurrent(std::string& error)
{
#if !SURREAL_BUILD_IDENTITY_AVAILABLE
	error = "build Git identity was unavailable at compile time";
	return {};
#else
	try
	{
		return TryCreate(SURREAL_BUILD_IDENTITY_COMMIT, SURREAL_BUILD_IDENTITY_TREE,
			SURREAL_BUILD_IDENTITY_DIRTY != 0, std::filesystem::read_symlink("/proc/self/exe").string(), error);
	}
	catch (...) {}
#ifdef WIN32
	char path[MAX_PATH]{};
	const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
	if (length != 0 && length < MAX_PATH)
		return TryCreate(SURREAL_BUILD_IDENTITY_COMMIT, SURREAL_BUILD_IDENTITY_TREE,
			SURREAL_BUILD_IDENTITY_DIRTY != 0, std::string(path, length), error);
#endif
	error = "could not resolve runtime executable path";
	return {};
#endif
}

std::string BotBenchmarkBuildIdentity::ToJson() const
{
	return "{\"schema\":\"surreal-engine-build-identity-v1\",\"id\":" + JsonString(IdentityId)
		+ ",\"source\":{\"commit\":" + JsonString(Commit) + ",\"tree\":" + JsonString(Tree)
		+ ",\"dirty\":" + (Dirty ? "true" : "false") + "},\"executable\":{\"name\":"
		+ JsonString(ExecutableName) + ",\"size_bytes\":" + JsonString(std::to_string(ExecutableSizeBytes))
		+ ",\"sha256\":" + JsonString(ExecutableSha256) + "}}";
}
