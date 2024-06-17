#include <gtest/gtest.h>
#include "SeseOmni.hpp"


TEST(SeseOmniTest, RadianHeadingCase)
{
	SeseOmni boat;
	for (int i = 0; i < 10000; i++)
	{
		boat.updateHeading();
		float iterativeHeading = boat.getIterativeHeading();
		EXPECT_TRUE((a >= 3.14) && (a <= 3.14));
	}
}
