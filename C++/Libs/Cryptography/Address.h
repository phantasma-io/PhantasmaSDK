﻿#pragma once

#include "../utils/Serializable.h"
#include "../Numerics/Base58.h"
#include "../Security/SecureString.h"
#include "EdDSA/Ed25519.h"

namespace phantasma {

class Address : public Serializable
{
public:
	static constexpr int TextLength = 45;
	static constexpr int PublicKeyLength = 32;
	static constexpr int MaxPlatformNameLength = 10;
	static constexpr Byte NullKey[PublicKeyLength] = {};

	const Byte* PublicKey() const
	{
		return _publicKey;
	}

	const String& Text() const
	{
		if(_text.empty())
			_text = Base58::Encode(&_opcode, PublicKeyLength+1);
		return _text;
	}

	Address()
	{
		PHANTASMA_COPY(NullKey, NullKey+PublicKeyLength, _publicKey);
	}

	Address(const Byte* publicKey, int length)
	{
		if(!publicKey || length != PublicKeyLength)
		{
			PHANTASMA_EXCEPTION("Invalid public key length");
			PHANTASMA_COPY(NullKey, NullKey+PublicKeyLength, _publicKey);
		}
		else
		{
			PHANTASMA_COPY(publicKey, publicKey+length, _publicKey);
			if (IsSystem)
			{
				_opcode = SystemOpcode;
			}
			else if (IsInterop)
			{
				_opcode = InteropOpcode;
			}
			else
			{
				_opcode = UserOpcode;
			}
		}
	}

	Address(const ByteArray& publicKey)
		: Address(&publicKey.front(), (int)publicKey.size())
	{}

	static Address FromHash(const String& str)
	{
		ByteArray temp;
		int numBytes = 0;
		const Byte* bytes = GetUTF8Bytes( str, temp, numBytes );
		return FromHash(bytes,numBytes);
	}

	static Address FromHash(const Byte* bytes, int length)
	{
		Byte hash[PHANTASMA_SHA256_LENGTH];
		SHA256( hash, PHANTASMA_SHA256_LENGTH, bytes, length );
		hash[0] = SystemOpcode;
		return Address(hash, PHANTASMA_SHA256_LENGTH);
	}

	bool IsNull() const { return  PHANTASMA_EQUAL(_publicKey, _publicKey + PublicKeyLength, NullKey); };
	bool IsSystem() const
	{
		return _publicKey[0] == (Byte)'!' || IsNull();
	}
	// NOTE currently we only support interop chain names with 3 chars, but this could be expanded to support up to 10 chars
	bool IsInterop() const
	{
		return !IsNull() && _publicKey[0] == (Byte)'*';
	}
	bool IsUser() const { return !IsSystem() && !IsInterop(); }
	
	bool operator ==( const Address& B ) const { return  PHANTASMA_EQUAL(_publicKey, _publicKey + PublicKeyLength, B._publicKey); }
	bool operator !=( const Address& B ) const { return !PHANTASMA_EQUAL(_publicKey, _publicKey + PublicKeyLength, B._publicKey); }

	String ToString() const
	{
		if (PHANTASMA_EQUAL(_publicKey, _publicKey + PublicKeyLength, NullKey))
		{
			return String(PHANTASMA_LITERAL("[Null address]"));
		}
		return Text();
	}

	static Address FromWIF(const SecureString& wif)
	{
		return FromWIF(wif.c_str(), wif.length());
	}
	static Address FromWIF(const Char* wif, int wifStringLength)
	{
		if( !wif || wif[0] == '\0' || wifStringLength <= 0 )
		{
			PHANTASMA_EXCEPTION( "WIF required" );
			return Address();
		}
		Byte publicKey[32];
		{
			PinnedBytes<34> data;
			int size = Base58::CheckDecodeSecure(data.bytes, 34, wif, wifStringLength);
			if( size != 34 || data.bytes[0] != 0x80 || data.bytes[33] != 0x01 )
			{
				PHANTASMA_EXCEPTION( "Invalid WIF format" );
				return Address();
			}
			Ed25519::PublicKeyFromSeed( publicKey, 32, &data.bytes[1], 32 );
		}
		return Address( publicKey, 32 );
	}

	static Address FromText(const String& text, bool* out_error=0)
	{
		return FromText(text.c_str(), (int)text.length(), out_error);
	}
	static Address FromText(const Char* text, int textLength=0, bool* out_error=0)
	{
		if(textLength == 0)
		{
			textLength = (int)PHANTASMA_STRLEN(text);
		}

		Byte bytes[PublicKeyLength+1];
		int decoded = 1;
		bool error = false;
		if(textLength != TextLength)
		{
			PHANTASMA_EXCEPTION("Invalid address length");
			error = true;
		}
		else
		{
			decoded = Base58::Decode(bytes, PublicKeyLength+1, text, textLength);
			if( decoded != PublicKeyLength+1 )
			{
				PHANTASMA_EXCEPTION("Invalid address encoding");
				error = true;
			}
			Byte opcode = bytes[0];
			if(opcode != UserOpcode && opcode != SystemOpcode && opcode != InteropOpcode)
			{
				PHANTASMA_EXCEPTION("Invalid address opcode");
				error = true;
			}
		}
		if( error )
		{
			if( out_error )
				*out_error = true;
			return Address();
		}
		return Address(bytes+1, decoded-1);
	}

	static Address FromScript(const ByteArray& script)
	{
		return Address(SHA256(script));
	}

	int GetSize() const
	{
		return PublicKeyLength;
	}

	static bool IsValidAddress(const String& text)
	{
		PHANTASMA_TRY
		{
			bool error = false;
			Address addr = Address::FromText(text, &error);
			return !error;
		}
		PHANTASMA_CATCH(...)
		{
			return false;
		}
	}

	template<class BinaryWriter>
	void SerializeData(BinaryWriter& writer) const
	{
		writer.WriteByteArray(_publicKey);
	}

	template<class BinaryReader>
	void UnserializeData(BinaryReader& reader)
	{
		reader.ReadByteArray(_publicKey);
		_text = "";
	}
	
	int DecodeInterop(String& platformName, Byte* data, int expectedDataLength)
	{
		if(expectedDataLength < 0)
		{
			PHANTASMA_EXCEPTION("invalid data length");
			return -1;
		}
		if(expectedDataLength > 27)
		{
			PHANTASMA_EXCEPTION("data is too large");
			return -1;
		}
		if(!IsInterop())
		{
			PHANTASMA_EXCEPTION("must be an interop address");
			return -1;
		}

		StringBuilder sb;
		int i = 1;
		while (true)
		{
			if (i >= PublicKeyLength)
			{
				PHANTASMA_EXCEPTION("invalid interop address");
				return -1;
			}
			Char ch = (Char)_publicKey[i];
			if (ch == '*')
			{
				break;
			}

			sb << ch;
			i++;
		}

		const String& str = sb.str();
		if (str.length() == 0)
		{
			PHANTASMA_EXCEPTION("invalid interop address");
			return -1;
		}

		i++;
		platformName = str;

		if (expectedDataLength > 0)
		{
			int n;
			for (n=0; n<expectedDataLength && i+n < PublicKeyLength; n++)
			{
				data[n] = _publicKey[i+n];
			}
			return n;
		}
		return 0;
	}

	static Address EncodeInterop(const String& platformName, const Byte* data, int dataLength)
	{
		if(platformName.length() == 0)
		{
			PHANTASMA_EXCEPTION("platform name cant be null");
			return {};
		}
		if(platformName.length() > MaxPlatformNameLength)
		{
			PHANTASMA_EXCEPTION("platform name is too big");
			return {};
		}

		Byte bytes[PublicKeyLength];
		bytes[0] = (Byte)'*';
		int i = 1;
		for(Char ch : platformName)
		{
			bytes[i] = (Byte)ch;
			i++;
		}
		bytes[i] = (Byte)'*';
		i++;

		for(int j=0; j<dataLength; ++j)
		{
			bytes[i] = (Byte)data[j];
			i++;
		}

		return Address(bytes, PublicKeyLength);
	}

private:
	static constexpr Byte UserOpcode = 75;
	static constexpr Byte SystemOpcode = 85;
	static constexpr Byte InteropOpcode = 102;

	Byte _opcode = 74;
	Byte _publicKey[PublicKeyLength];
	mutable String _text;
};

}
