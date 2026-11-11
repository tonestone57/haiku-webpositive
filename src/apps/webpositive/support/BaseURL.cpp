/*
 * Copyright 2010 Stephan Aßmus <superstippi@gmx.de>
 * All rights reserved. Distributed under the terms of the MIT License.
 */

#include "BaseURL.h"


BString
baseURL(const BString string)
{
	int32 schemePos = string.FindFirst("://");
	int32 baseURLStart = (schemePos >= 0) ? (schemePos + 3) : 0;
	int32 baseURLEnd = string.FindFirst("/", baseURLStart);

	BString result;
	if (baseURLEnd >= baseURLStart) {
		result.SetTo(string.String() + baseURLStart, baseURLEnd - baseURLStart);
	} else {
		result.SetTo(string.String() + baseURLStart, string.Length() - baseURLStart);
	}
	return result;
}

