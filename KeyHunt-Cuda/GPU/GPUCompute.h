/*
 * This file is part of the VanitySearch distribution (https://github.com/JeanLucPons/VanitySearch).
 * Copyright (c) 2019 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <device_atomic_functions.h>
#include <device_functions.h>

__device__ int test_bit_set_bit(const uint8_t* buf, uint32_t bit)
{
	uint32_t byte = bit >> 3;
	uint8_t c = buf[byte];        // expensive memory access
	uint8_t mask = 1 << (bit % 8);

	if (c & mask) {
		return 1;
	}
	else {
		return 0;
	}
}

__device__ uint32_t murmurhash2(const void* key, int len, uint32_t seed)
{
	const uint32_t m = 0x5bd1e995;
	const int r = 24;

	uint32_t h = seed ^ len;
	const uint8_t* data = (const uint8_t*)key;
	while (len >= 4) {
		uint32_t k = *(uint32_t*)data;
		k *= m;
		k ^= k >> r;
		k *= m;
		h *= m;
		h ^= k;
		data += 4;
		len -= 4;
	}
	switch (len) {
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
		h *= m;
	}

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
}

__device__ int BloomCheck(const uint32_t* hash, const uint8_t* inputBloomLookUp, uint64_t BLOOM_BITS, uint8_t BLOOM_HASHES)
{
	int add = 0;
	uint8_t hits = 0;
	uint32_t a = murmurhash2((uint8_t*)hash, 20, 0x9747b28c);
	uint32_t b = murmurhash2((uint8_t*)hash, 20, a);
	uint32_t x;
	uint8_t i;
	for (i = 0; i < BLOOM_HASHES; i++) {
		x = (a + b * i) % BLOOM_BITS;
		if (test_bit_set_bit(inputBloomLookUp, x)) {
			hits++;
		}
		else if (!add) {
			return 0;
		}
	}
	if (hits == BLOOM_HASHES) {
		return 1;
	}
	return 0;
}

__device__ __noinline__ void ClearCouter(uint32_t* out)
{
	uint32_t tid = (blockIdx.x * blockDim.x) + threadIdx.x;
	uint32_t pos = atomicAnd(out, 0);
}

__device__ __noinline__ void CheckPoint(uint32_t* _h, int32_t incr, int32_t endo, int32_t mode,
	uint8_t* bloomLookUp, uint64_t BLOOM_BITS, uint8_t BLOOM_HASHES, uint32_t maxFound, uint32_t* out, int type)
{
	uint32_t tid = (blockIdx.x * blockDim.x) + threadIdx.x;

	if (BloomCheck(_h, bloomLookUp, BLOOM_BITS, BLOOM_HASHES) > 0) {
		uint32_t pos = atomicAdd(out, 1);
		if (pos < maxFound) {
			out[pos * ITEM_SIZE32 + 1] = tid;
			out[pos * ITEM_SIZE32 + 2] = (uint32_t)(incr << 16) | (uint32_t)(mode << 15) | (uint32_t)(endo);
			out[pos * ITEM_SIZE32 + 3] = _h[0];
			out[pos * ITEM_SIZE32 + 4] = _h[1];
			out[pos * ITEM_SIZE32 + 5] = _h[2];
			out[pos * ITEM_SIZE32 + 6] = _h[3];
			out[pos * ITEM_SIZE32 + 7] = _h[4];
		}
	}
}

__device__ __noinline__ bool MatchHash160(uint32_t* _h, uint32_t* hash160)
{
	if (_h[0] == hash160[0] &&
		_h[1] == hash160[1] &&
		_h[2] == hash160[2] &&
		_h[3] == hash160[3] &&
		_h[4] == hash160[4]) {
		return true;
	}
	else {
		return false;
	}
}

__device__ __noinline__ void CheckPoint2(uint32_t* _h, int32_t incr, int32_t endo, int32_t mode,
	uint32_t* hash160, uint32_t maxFound, uint32_t* out, int type)
{
	uint32_t tid = (blockIdx.x * blockDim.x) + threadIdx.x;

	if (MatchHash160(_h, hash160)) {
		uint32_t pos = atomicAdd(out, 1);
		if (pos < maxFound) {
			out[pos * ITEM_SIZE32 + 1] = tid;
			out[pos * ITEM_SIZE32 + 2] = (uint32_t)(incr << 16) | (uint32_t)(mode << 15) | (uint32_t)(endo);
			out[pos * ITEM_SIZE32 + 3] = _h[0];
			out[pos * ITEM_SIZE32 + 4] = _h[1];
			out[pos * ITEM_SIZE32 + 5] = _h[2];
			out[pos * ITEM_SIZE32 + 6] = _h[3];
			out[pos * ITEM_SIZE32 + 7] = _h[4];
		}
	}
}

// -----------------------------------------------------------------------------------------

#define CHECK_POINT(_h,incr,endo,mode)  CheckPoint(_h,incr,endo,mode,bloomLookUp,BLOOM_BITS,BLOOM_HASHES,maxFound,out,P2PKH)

__device__ __noinline__ void CheckHashComp(uint64_t* px, uint8_t isOdd, int32_t incr,
	uint8_t* bloomLookUp, int BLOOM_BITS, uint8_t BLOOM_HASHES, uint32_t maxFound, uint32_t* out)
{

	uint32_t   h[5];
	uint64_t   pe1x[4];
	uint64_t   pe2x[4];

	_GetHash160Comp(px, isOdd, (uint8_t*)h);
	CHECK_POINT(h, incr, 0, true);
	_ModMult(pe1x, px, _beta);
	_GetHash160Comp(pe1x, isOdd, (uint8_t*)h);
	CHECK_POINT(h, incr, 1, true);
	_ModMult(pe2x, px, _beta2);
	_GetHash160Comp(pe2x, isOdd, (uint8_t*)h);
	CHECK_POINT(h, incr, 2, true);

	_GetHash160Comp(px, !isOdd, (uint8_t*)h);
	CHECK_POINT(h, -incr, 0, true);
	_GetHash160Comp(pe1x, !isOdd, (uint8_t*)h);
	CHECK_POINT(h, -incr, 1, true);
	_GetHash160Comp(pe2x, !isOdd, (uint8_t*)h);
	CHECK_POINT(h, -incr, 2, true);


}


#define CHECK_POINT2(_h,incr,endo,mode)  CheckPoint2(_h,incr,endo,mode,hash160,maxFound,out,P2PKH)

__device__ __noinline__ void CheckHashComp2(uint64_t* px, uint8_t isOdd, int32_t incr,
	uint32_t* hash160, uint32_t maxFound, uint32_t* out)
{

	uint32_t   h[5];
	uint64_t   pe1x[4];
	uint64_t   pe2x[4];

	_GetHash160Comp(px, isOdd, (uint8_t*)h);
	CHECK_POINT2(h, incr, 0, true);
	_ModMult(pe1x, px, _beta);
	_GetHash160Comp(pe1x, isOdd, (uint8_t*)h);
	CHECK_POINT2(h, incr, 1, true);
	_ModMult(pe2x, px, _beta2);
	_GetHash160Comp(pe2x, isOdd, (uint8_t*)h);
	CHECK_POINT2(h, incr, 2, true);

	_GetHash160Comp(px, !isOdd, (uint8_t*)h);
	CHECK_POINT2(h, -incr, 0, true);
	_GetHash160Comp(pe1x, !isOdd, (uint8_t*)h);
	CHECK_POINT2(h, -incr, 1, true);
	_GetHash160Comp(pe2x, !isOdd, (uint8_t*)h);
	CHECK_POINT2(h, -incr, 2, true);


}
// -----------------------------------------------------------------------------------------

__device__ __noinline__ void CheckHashUncomp(uint64_t* px, uint64_t* py, int32_t incr,
	uint8_t* bloomLookUp, int BLOOM_BITS, uint8_t BLOOM_HASHES, uint32_t maxFound, uint32_t* out)
{

	uint32_t   h[5];
	uint64_t   pe1x[4];
	uint64_t   pe2x[4];
	uint64_t   pyn[4];

	_GetHash160(px, py, (uint8_t*)h);
	CHECK_POINT(h, incr, 0, false);
	_ModMult(pe1x, px, _beta);
	_GetHash160(pe1x, py, (uint8_t*)h);
	CHECK_POINT(h, incr, 1, false);
	_ModMult(pe2x, px, _beta2);
	_GetHash160(pe2x, py, (uint8_t*)h);
	CHECK_POINT(h, incr, 2, false);

	ModNeg256(pyn, py);

	_GetHash160(px, pyn, (uint8_t*)h);
	CHECK_POINT(h, -incr, 0, false);
	_GetHash160(pe1x, pyn, (uint8_t*)h);
	CHECK_POINT(h, -incr, 1, false);
	_GetHash160(pe2x, pyn, (uint8_t*)h);
	CHECK_POINT(h, -incr, 2, false);

}

__device__ __noinline__ void CheckHashUncomp2(uint64_t* px, uint64_t* py, int32_t incr,
	uint32_t* hash160, uint32_t maxFound, uint32_t* out)
{

	uint32_t   h[5];
	uint64_t   pe1x[4];
	uint64_t   pe2x[4];
	uint64_t   pyn[4];

	_GetHash160(px, py, (uint8_t*)h);
	CHECK_POINT2(h, incr, 0, false);
	_ModMult(pe1x, px, _beta);
	_GetHash160(pe1x, py, (uint8_t*)h);
	CHECK_POINT2(h, incr, 1, false);
	_ModMult(pe2x, px, _beta2);
	_GetHash160(pe2x, py, (uint8_t*)h);
	CHECK_POINT2(h, incr, 2, false);

	ModNeg256(pyn, py);

	_GetHash160(px, pyn, (uint8_t*)h);
	CHECK_POINT2(h, -incr, 0, false);
	_GetHash160(pe1x, pyn, (uint8_t*)h);
	CHECK_POINT2(h, -incr, 1, false);
	_GetHash160(pe2x, pyn, (uint8_t*)h);
	CHECK_POINT2(h, -incr, 2, false);

}

// -----------------------------------------------------------------------------------------

__device__ __noinline__ void CheckHash(uint32_t mode, uint64_t* px, uint64_t* py, int32_t incr,
	uint8_t* bloomLookUp, int BLOOM_BITS, uint8_t BLOOM_HASHES, uint32_t maxFound, uint32_t* out)
{

	switch (mode) {
	case SEARCH_COMPRESSED:
		CheckHashComp(px, (uint8_t)(py[0] & 1), incr, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out);
		break;
	case SEARCH_UNCOMPRESSED:
		CheckHashUncomp(px, py, incr, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out);
		break;
	case SEARCH_BOTH:
		CheckHashComp(px, (uint8_t)(py[0] & 1), incr, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out);
		CheckHashUncomp(px, py, incr, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out);
		break;
	}

}


#define CHECK_PREFIX(incr) CheckHash(mode, px, py, j*GRP_SIZE + (incr), bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out)

// -----------------------------------------------------------------------------------------

__device__ void ComputeKeys(uint32_t mode, uint64_t* startx, uint64_t* starty,
	uint8_t* bloomLookUp, int BLOOM_BITS, uint8_t BLOOM_HASHES, uint32_t maxFound, uint32_t* out)
{

	uint64_t dx[GRP_SIZE / 2 + 1][4];
	uint64_t px[4];
	uint64_t py[4];
	uint64_t pyn[4];
	uint64_t sx[4];
	uint64_t sy[4];
	uint64_t dy[4];
	uint64_t _s[4];
	uint64_t _p2[4];
	//char pattern[48];

	// Load starting key
	__syncthreads();
	Load256A(sx, startx);
	Load256A(sy, starty);
	Load256(px, sx);
	Load256(py, sy);

	//if (sPrefix == NULL) {
	//	memcpy(pattern, lookup32, 48);
	//	lookup32 = (uint32_t *)pattern;
	//}

	for (uint32_t j = 0; j < STEP_SIZE / GRP_SIZE; j++) {

		// Fill group with delta x
		uint32_t i;
		for (i = 0; i < HSIZE; i++)
			ModSub256(dx[i], Gx[i], sx);
		ModSub256(dx[i], Gx[i], sx);   // For the first point
		ModSub256(dx[i + 1], _2Gnx, sx); // For the next center point

		// Compute modular inverse
		_ModInvGrouped(dx);

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group

		// Check starting point
		CHECK_PREFIX(GRP_SIZE / 2);

		ModNeg256(pyn, py);

		for (i = 0; i < HSIZE; i++) {

			// P = StartPoint + i*G
			Load256(px, sx);
			Load256(py, sy);
			ModSub256(dy, Gy[i], py);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p2 = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			ModSub256(py, Gx[i], px);
			_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
			ModSub256(py, Gy[i]);         // py = - p2.y - s*(ret.x-p2.x);

			CHECK_PREFIX(GRP_SIZE / 2 + (i + 1));

			// P = StartPoint - i*G, if (x,y) = i*G then (x,-y) = -i*G
			Load256(px, sx);
			ModSub256(dy, pyn, Gy[i]);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			ModSub256(py, px, Gx[i]);
			_ModMult(py, _s);             // py = s*(ret.x-p2.x)
			ModSub256(py, Gy[i], py);     // py = - p2.y - s*(ret.x-p2.x);

			CHECK_PREFIX(GRP_SIZE / 2 - (i + 1));

		}

		// First point (startP - (GRP_SZIE/2)*G)
		Load256(px, sx);
		Load256(py, sy);
		ModNeg256(dy, Gy[i]);
		ModSub256(dy, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

		ModSub256(py, px, Gx[i]);
		_ModMult(py, _s);             // py = s*(ret.x-p2.x)
		ModSub256(py, Gy[i], py);     // py = - p2.y - s*(ret.x-p2.x);

		CHECK_PREFIX(0);

		i++;

		// Next start point (startP + GRP_SIZE*G)
		Load256(px, sx);
		Load256(py, sy);
		ModSub256(dy, _2Gny, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p2 = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, _2Gnx);         // px = pow2(s) - p1.x - p2.x;

		ModSub256(py, _2Gnx, px);
		_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
		ModSub256(py, _2Gny);         // py = - p2.y - s*(ret.x-p2.x);

	}

	// Update starting point
	__syncthreads();
	Store256A(startx, px);
	Store256A(starty, py);

}

// -----------------------------------------------------------------------------------------

__device__ __noinline__ void CheckHash2(uint32_t mode, uint64_t* px, uint64_t* py, int32_t incr,
	uint32_t* hash160, uint32_t maxFound, uint32_t* out)
{

	switch (mode) {
	case SEARCH_COMPRESSED:
		CheckHashComp2(px, (uint8_t)(py[0] & 1), incr, hash160, maxFound, out);
		break;
	case SEARCH_UNCOMPRESSED:
		CheckHashUncomp2(px, py, incr, hash160, maxFound, out);
		break;
	case SEARCH_BOTH:
		CheckHashComp2(px, (uint8_t)(py[0] & 1), incr, hash160, maxFound, out);
		CheckHashUncomp2(px, py, incr, hash160, maxFound, out);
		break;
	}

}
// -----------------------------------------------------------------------------------------

#define CHECK_PREFIX2(incr) CheckHash2(mode, px, py, j*GRP_SIZE + (incr), hash160, maxFound, out)

// -----------------------------------------------------------------------------------------

__device__ void ComputeKeys2(uint32_t mode, uint64_t* startx, uint64_t* starty,
	uint32_t* hash160, uint32_t maxFound, uint32_t* out)
{

	uint64_t dx[GRP_SIZE / 2 + 1][4];
	uint64_t px[4];
	uint64_t py[4];
	uint64_t pyn[4];
	uint64_t sx[4];
	uint64_t sy[4];
	uint64_t dy[4];
	uint64_t _s[4];
	uint64_t _p2[4];
	//char pattern[48];

	// Load starting key
	__syncthreads();
	Load256A(sx, startx);
	Load256A(sy, starty);
	Load256(px, sx);
	Load256(py, sy);

	for (uint32_t j = 0; j < STEP_SIZE / GRP_SIZE; j++) {

		// Fill group with delta x
		uint32_t i;
		for (i = 0; i < HSIZE; i++)
			ModSub256(dx[i], Gx[i], sx);
		ModSub256(dx[i], Gx[i], sx);   // For the first point
		ModSub256(dx[i + 1], _2Gnx, sx); // For the next center point

		// Compute modular inverse
		_ModInvGrouped(dx);

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group

		// Check starting point
		CHECK_PREFIX2(GRP_SIZE / 2);

		ModNeg256(pyn, py);

		for (i = 0; i < HSIZE; i++) {

			// P = StartPoint + i*G
			Load256(px, sx);
			Load256(py, sy);
			ModSub256(dy, Gy[i], py);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p2 = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			ModSub256(py, Gx[i], px);
			_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
			ModSub256(py, Gy[i]);         // py = - p2.y - s*(ret.x-p2.x);

			CHECK_PREFIX2(GRP_SIZE / 2 + (i + 1));

			// P = StartPoint - i*G, if (x,y) = i*G then (x,-y) = -i*G
			Load256(px, sx);
			ModSub256(dy, pyn, Gy[i]);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			ModSub256(py, px, Gx[i]);
			_ModMult(py, _s);             // py = s*(ret.x-p2.x)
			ModSub256(py, Gy[i], py);     // py = - p2.y - s*(ret.x-p2.x);

			CHECK_PREFIX2(GRP_SIZE / 2 - (i + 1));

		}

		// First point (startP - (GRP_SZIE/2)*G)
		Load256(px, sx);
		Load256(py, sy);
		ModNeg256(dy, Gy[i]);
		ModSub256(dy, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

		ModSub256(py, px, Gx[i]);
		_ModMult(py, _s);             // py = s*(ret.x-p2.x)
		ModSub256(py, Gy[i], py);     // py = - p2.y - s*(ret.x-p2.x);

		CHECK_PREFIX2(0);

		i++;

		// Next start point (startP + GRP_SIZE*G)
		Load256(px, sx);
		Load256(py, sy);
		ModSub256(dy, _2Gny, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p2 = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, _2Gnx);         // px = pow2(s) - p1.x - p2.x;

		ModSub256(py, _2Gnx, px);
		_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
		ModSub256(py, _2Gny);         // py = - p2.y - s*(ret.x-p2.x);

	}

	// Update starting point
	__syncthreads();
	Store256A(startx, px);
	Store256A(starty, py);

}

// -----------------------------------------------------------------------------------------
// Optimized kernel for compressed P2PKH address only

#define CHECK_P2PKH_POINT(_incr) {                                                                 \
_GetHash160CompSym(px, (uint8_t *)h1, (uint8_t *)h2);                                              \
CheckPoint(h1, (_incr), 0, true, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out, P2PKH);     \
CheckPoint(h2, -(_incr), 0, true, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out, P2PKH);    \
_ModMult(pe1x, px, _beta);                                                                         \
_GetHash160CompSym(pe1x, (uint8_t *)h1, (uint8_t *)h2);                                            \
CheckPoint(h1, (_incr), 1, true, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out, P2PKH);     \
CheckPoint(h2, -(_incr), 1, true, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out, P2PKH);    \
_ModMult(pe2x, px, _beta2);                                                                        \
_GetHash160CompSym(pe2x, (uint8_t *)h1, (uint8_t *)h2);                                            \
CheckPoint(h1, (_incr), 2, true, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out, P2PKH);     \
CheckPoint(h2, -(_incr), 2, true, bloomLookUp, BLOOM_BITS, BLOOM_HASHES, maxFound, out, P2PKH);    \
}

__device__ void ComputeKeysComp(uint64_t* startx, uint64_t* starty, uint8_t* bloomLookUp,
	int BLOOM_BITS, uint8_t BLOOM_HASHES, uint32_t maxFound, uint32_t* out)
{

	uint64_t dx[GRP_SIZE / 2 + 1][4];
	uint64_t px[4];
	uint64_t py[4];
	uint64_t pyn[4];
	uint64_t sx[4];
	uint64_t sy[4];
	uint64_t dy[4];
	uint64_t _s[4];
	uint64_t _p2[4];
	uint32_t h1[5];
	uint32_t h2[5];
	uint64_t pe1x[4];
	uint64_t pe2x[4];

	// Load starting key
	__syncthreads();
	Load256A(sx, startx);
	Load256A(sy, starty);
	Load256(px, sx);
	Load256(py, sy);

	for (uint32_t j = 0; j < STEP_SIZE / GRP_SIZE; j++) {

		// Fill group with delta x
		uint32_t i;
		for (i = 0; i < HSIZE; i++)
			ModSub256(dx[i], Gx[i], sx);
		ModSub256(dx[i], Gx[i], sx);   // For the first point
		ModSub256(dx[i + 1], _2Gnx, sx); // For the next center point

		// Compute modular inverse
		_ModInvGrouped(dx);

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group

		// Check starting point
		CHECK_P2PKH_POINT(j * GRP_SIZE + (GRP_SIZE / 2));

		ModNeg256(pyn, py);

		for (i = 0; i < HSIZE; i++) {

			__syncthreads();
			// P = StartPoint + i*G
			Load256(px, sx);
			Load256(py, sy);
			ModSub256(dy, Gy[i], py);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p2 = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			CHECK_P2PKH_POINT(j * GRP_SIZE + (GRP_SIZE / 2 + (i + 1)));

			__syncthreads();
			// P = StartPoint - i*G, if (x,y) = i*G then (x,-y) = -i*G
			Load256(px, sx);
			ModSub256(dy, pyn, Gy[i]);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			CHECK_P2PKH_POINT(j * GRP_SIZE + (GRP_SIZE / 2 - (i + 1)));

		}

		__syncthreads();
		// First point (startP - (GRP_SZIE/2)*G)
		Load256(px, sx);
		Load256(py, sy);
		ModNeg256(dy, Gy[i]);
		ModSub256(dy, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

		CHECK_P2PKH_POINT(j * GRP_SIZE + (0));

		i++;

		__syncthreads();
		// Next start point (startP + GRP_SIZE*G)
		Load256(px, sx);
		Load256(py, sy);
		ModSub256(dy, _2Gny, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p2 = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, _2Gnx);         // px = pow2(s) - p1.x - p2.x;

		ModSub256(py, _2Gnx, px);
		_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
		ModSub256(py, _2Gny);         // py = - p2.y - s*(ret.x-p2.x);

	}

	// Update starting point
	__syncthreads();
	Store256A(startx, px);
	Store256A(starty, py);

}



#define CHECK_P2PKH_POINT2(_incr) {                                   \
_GetHash160CompSym(px, (uint8_t *)h1, (uint8_t *)h2);                 \
CheckPoint2(h1, (_incr), 0, true, hash160, maxFound, out, P2PKH);     \
CheckPoint2(h2, -(_incr), 0, true, hash160, maxFound, out, P2PKH);    \
_ModMult(pe1x, px, _beta);                                            \
_GetHash160CompSym(pe1x, (uint8_t *)h1, (uint8_t *)h2);               \
CheckPoint2(h1, (_incr), 1, true, hash160, maxFound, out, P2PKH);     \
CheckPoint2(h2, -(_incr), 1, true, hash160, maxFound, out, P2PKH);    \
_ModMult(pe2x, px, _beta2);                                           \
_GetHash160CompSym(pe2x, (uint8_t *)h1, (uint8_t *)h2);               \
CheckPoint2(h1, (_incr), 2, true, hash160, maxFound, out, P2PKH);     \
CheckPoint2(h2, -(_incr), 2, true, hash160, maxFound, out, P2PKH);    \
}


__device__ void ComputeKeysComp2(uint64_t* startx, uint64_t* starty,
	uint32_t* hash160, uint32_t maxFound, uint32_t* out)
{

	uint64_t dx[GRP_SIZE / 2 + 1][4];
	uint64_t px[4];
	uint64_t py[4];
	uint64_t pyn[4];
	uint64_t sx[4];
	uint64_t sy[4];
	uint64_t dy[4];
	uint64_t _s[4];
	uint64_t _p2[4];
	uint32_t h1[5];
	uint32_t h2[5];
	uint64_t pe1x[4];
	uint64_t pe2x[4];

	// Load starting key
	__syncthreads();
	Load256A(sx, startx);
	Load256A(sy, starty);
	Load256(px, sx);
	Load256(py, sy);

	for (uint32_t j = 0; j < STEP_SIZE / GRP_SIZE; j++) {

		// Fill group with delta x
		uint32_t i;
		for (i = 0; i < HSIZE; i++)
			ModSub256(dx[i], Gx[i], sx);
		ModSub256(dx[i], Gx[i], sx);   // For the first point
		ModSub256(dx[i + 1], _2Gnx, sx); // For the next center point

		// Compute modular inverse
		_ModInvGrouped(dx);

		// We use the fact that P + i*G and P - i*G has the same deltax, so the same inverse
		// We compute key in the positive and negative way from the center of the group

		// Check starting point
		CHECK_P2PKH_POINT2(j * GRP_SIZE + (GRP_SIZE / 2));

		ModNeg256(pyn, py);

		for (i = 0; i < HSIZE; i++) {

			__syncthreads();
			// P = StartPoint + i*G
			Load256(px, sx);
			Load256(py, sy);
			ModSub256(dy, Gy[i], py);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p2 = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			CHECK_P2PKH_POINT2(j * GRP_SIZE + (GRP_SIZE / 2 + (i + 1)));

			__syncthreads();
			// P = StartPoint - i*G, if (x,y) = i*G then (x,-y) = -i*G
			Load256(px, sx);
			ModSub256(dy, pyn, Gy[i]);

			_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
			_ModSqr(_p2, _s);             // _p = pow2(s)

			ModSub256(px, _p2, px);
			ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

			CHECK_P2PKH_POINT2(j * GRP_SIZE + (GRP_SIZE / 2 - (i + 1)));

		}

		__syncthreads();
		// First point (startP - (GRP_SZIE/2)*G)
		Load256(px, sx);
		Load256(py, sy);
		ModNeg256(dy, Gy[i]);
		ModSub256(dy, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, Gx[i]);         // px = pow2(s) - p1.x - p2.x;

		CHECK_P2PKH_POINT2(j * GRP_SIZE + (0));

		i++;

		__syncthreads();
		// Next start point (startP + GRP_SIZE*G)
		Load256(px, sx);
		Load256(py, sy);
		ModSub256(dy, _2Gny, py);

		_ModMult(_s, dy, dx[i]);      //  s = (p2.y-p1.y)*inverse(p2.x-p1.x)
		_ModSqr(_p2, _s);             // _p2 = pow2(s)

		ModSub256(px, _p2, px);
		ModSub256(px, _2Gnx);         // px = pow2(s) - p1.x - p2.x;

		ModSub256(py, _2Gnx, px);
		_ModMult(py, _s);             // py = - s*(ret.x-p2.x)
		ModSub256(py, _2Gny);         // py = - p2.y - s*(ret.x-p2.x);

	}

	// Update starting point
	__syncthreads();
	Store256A(startx, px);
	Store256A(starty, py);

}
