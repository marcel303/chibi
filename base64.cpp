#include "base64.h"
#include <assert.h>
#include <stdint.h>

namespace chibi
{
	static const char encodingTable[64] =
	{
		'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
		'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
		'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
		'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
	};

	std::string base64_encode(const void * in_bytes, const size_t numBytes)
	{
		std::string result;
		
		if (numBytes == 0)
			return result;

		const uint8_t * bytes = (const uint8_t*)in_bytes;
		const size_t byteCount = numBytes;
		
		const size_t expectedCharacterCount = (byteCount * 8 + 5) / 6;
		const size_t expectedResultSize = (expectedCharacterCount + 3) / 4 * 4;
		result.reserve(expectedResultSize);
		
		size_t baseIndex = 0;
		
		unsigned char inbuf[3];
		unsigned char outbuf[4];
		
		for (;;)
		{
			if (baseIndex >= byteCount)
				break;

			const size_t todo = byteCount - baseIndex;
			
			for (int i = 0; i < 3; i++)
			{
				const size_t index = baseIndex + i;
				
				if (index < byteCount)
					inbuf[i] = bytes[index];
				else
					inbuf[i] = 0;
			}
			
			outbuf[0] = (inbuf [0] & 0xFC) >> 2;
			outbuf[1] = ((inbuf [0] & 0x03) << 4) | ((inbuf [1] & 0xF0) >> 4);
			outbuf[2] = ((inbuf [1] & 0x0F) << 2) | ((inbuf [2] & 0xC0) >> 6);
			outbuf[3] = inbuf [2] & 0x3F;
			
			int copyCount = 4;
			
			switch (todo)
			{
				case 1: 
					copyCount = 2; 
					break;
				case 2: 
					copyCount = 3; 
					break;
			}
			
			for (int i = 0; i < copyCount; i++)
				result.push_back(encodingTable[outbuf[i]]);
			
			for (int i = copyCount; i < 4; i++)
				result.push_back('=');
			
			baseIndex += 3;
		}
		
		assert(result.size() == expectedResultSize); // if unequal, we either reserved too much memory, or had to grow/realloc the resulting array
		
		return result;
	}
}
