/** \file
 * \brief Mathematical Helpers
 *
 * \author Markus Chimani, Ivo Hedtke
 *
 * \par License:
 * This file is part of the Open Graph Drawing Framework (OGDF).
 *
 * \par
 * Copyright (C)<br>
 * See README.txt in the root directory of the OGDF installation for details.
 *
 * \par
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 or 3 as published by the Free Software Foundation;
 * see the file LICENSE.txt included in the packaging of this file
 * for details.
 *
 * \par
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * \par
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * \see  http://www.gnu.org/copyleft/gpl.html
 ***************************************************************/

#ifndef OGDF_MATH_H
#define OGDF_MATH_H

#include <ogdf/basic/basic.h>
#include <ogdf/basic/Stack.h>
#include <ogdf/basic/Array.h>


namespace ogdf {


class OGDF_EXPORT Math {

public:
	//! The constant \f$\pi\f$.
	static const double pi;

	//! The constant \f$\frac{\pi}{2}\f$.
	static const double pi_2;

	//! The constant \f$\frac{\pi}{4}\f$.
	static const double pi_4;

	//! The constant \f$2\pi\f$.
	static const double two_pi;

	//! Euler's number.
	static const double e;

	//! The constant log(2.0).
	static const double log_of_2;

	//! The constant log(4.0).
	static const double log_of_4;

    //! Returns the logarithm of \a x to the base 2.
    template<typename T>
    static T log2(T x) {
        OGDF_ASSERT(x >= 0);
        return std::log2(x);
    }

	//! Returns the logarithm of \a x to the base 4.
	static double log4(double x) {
		OGDF_ASSERT(x >= 0)
		return log(x) / log_of_4;
	}

	//! Returns \f$n \choose k\f$.
	static int binomial(int n, int k);

	//! Returns \f$n \choose k\f$.
	static double binomial_d(int n, int k);

	//! Returns \a n!.
	static int factorial(int n);

	//! Returns \a n!.
	static double factorial_d(int n);

	//static bool equald(double a, double b) {
	//	double d = a-b;
	//	return d < DOUBLE_EPS && d > -DOUBLE_EPS;
	//}

	/*!
	 * \brief A fast method to obtain the rounded down binary logarithm of an 32-bit integer
	 *
	 * This is based on http://en.wikipedia.org/wiki/Binary_logarithm
	 * @param v The number of which the binary logarithm is to be determined
	 * @return The rounded down logarithm base 2 if v is positive, -1 otherwise
	 */
	static int floorLog2(int v) {
		if (v <= 0) {
			return -1;
		} else {
			int result = 0;
			if (v >= (1 << 16)) {
				v >>= 16;
				result += 16;
			}
			if (v >= (1 << 8)) {
				v >>= 8;
				result += 8;
			}
			if (v >= (1 << 4)) {
				v >>= 4;
				result += 4;
			}
			if (v >= (1 << 2)) {
				v >>= 2;
				result += 2;
			}
			if (v >= (1 << 1)) {
				result += 1;
			}
			return result;
		}
	}

	//! Returns the greatest common divisor of two numbers.
    template<typename T>
	static T gcd(T a, T b)
	{
		// If b > a, they will be swapped in the first iteration.
		do {
			T c = a % b;
			a = b;
			b = c;
		} while (b > 0);
		return a;
	}

    //! Returns the greatest common divisor of a list of numbers.
    template<class T, class INDEX = int>
    static T gcd(const Array<T,INDEX> &numbers) {
        T current_gcd = numbers[numbers.low()];
        for (INDEX i = numbers.low()+1; i<=numbers.high(); i++) {
            current_gcd = gcd(current_gcd, numbers[i]);
        }
        return current_gcd;
    }

    //! Returns the least common multipler of two numbers.
    template<typename T>
    static T lcm(T a, T b) {
        return a*b / gcd(a,b);
    }

	//! Converts a double to a fraction.
	static void getFraction(double d, int &num, int &denom, const double epsilon = 5e-10, const int count = 10)
	{
		Stack<int> continuedFrac;

		// build continued fraction
		int z((int)d);
		continuedFrac.push(z);
		d = d - z;
		int i = 0;
		while (d > epsilon && i++ < count) {
			d = 1 / d;
			z = (int)d;
			continuedFrac.push(z);
			d = d - z;
		}

		// simplify continued fraction to simple fraction
		num = 1;
		denom = 0;
		while (!continuedFrac.empty()) {
			int last = continuedFrac.pop();
			swap(num, denom);
			num += last * denom;
		}
	}
};


}

#endif // OGDF_MATH_H
