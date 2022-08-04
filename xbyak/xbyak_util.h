#ifndef XBYAK_XBYAK_UTIL_H_
#define XBYAK_XBYAK_UTIL_H_

#ifdef XBYAK_ONLY_CLASS_CPU
#include <stdint.h>
#include <stdlib.h>
#include <algorithm>
#include <assert.h>
#ifndef XBYAK_THROW
	#define XBYAK_THROW(x) ;
	#define XBYAK_THROW_RET(x, y) return y;
#endif
#else
#include <string.h>

/**
	utility class and functions for Xbyak
	Xbyak::util::Clock ; rdtsc timer
	Xbyak::util::Cpu ; detect CPU
*/
#include "xbyak.h"
#endif // XBYAK_ONLY_CLASS_CPU

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
	#define XBYAK_INTEL_CPU_SPECIFIC
#endif

#ifdef XBYAK_INTEL_CPU_SPECIFIC
#ifdef _WIN32
	#if defined(_MSC_VER) && (_MSC_VER < 1400) && defined(XBYAK32)
		static inline __declspec(naked) void __cpuid(int[4], int)
		{
			__asm {
				push	ebx
				push	esi
				mov		eax, dword ptr [esp + 4 * 2 + 8] // eaxIn
				cpuid
				mov		esi, dword ptr [esp + 4 * 2 + 4] // data
				mov		dword ptr [esi], eax
				mov		dword ptr [esi + 4], ebx
				mov		dword ptr [esi + 8], ecx
				mov		dword ptr [esi + 12], edx
				pop		esi
				pop		ebx
				ret
			}
		}
	#else
		#include <intrin.h> // for __cpuid
	#endif
#else
	#ifndef __GNUC_PREREQ
    	#define __GNUC_PREREQ(major, minor) ((((__GNUC__) << 16) + (__GNUC_MINOR__)) >= (((major) << 16) + (minor)))
	#endif
	#if __GNUC_PREREQ(4, 3) && !defined(__APPLE__)
		#include <cpuid.h>
	#else
		#if defined(__APPLE__) && defined(XBYAK32) // avoid err : can't find a register in class `BREG' while reloading `asm'
			#define __cpuid(eaxIn, a, b, c, d) __asm__ __volatile__("pushl %%ebx\ncpuid\nmovl %%ebp, %%esi\npopl %%ebx" : "=a"(a), "=S"(b), "=c"(c), "=d"(d) : "0"(eaxIn))
			#define __cpuid_count(eaxIn, ecxIn, a, b, c, d) __asm__ __volatile__("pushl %%ebx\ncpuid\nmovl %%ebp, %%esi\npopl %%ebx" : "=a"(a), "=S"(b), "=c"(c), "=d"(d) : "0"(eaxIn), "2"(ecxIn))
		#else
			#define __cpuid(eaxIn, a, b, c, d) __asm__ __volatile__("cpuid\n" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(eaxIn))
			#define __cpuid_count(eaxIn, ecxIn, a, b, c, d) __asm__ __volatile__("cpuid\n" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(eaxIn), "2"(ecxIn))
		#endif
	#endif
#endif
#endif

#ifdef XBYAK_USE_VTUNE
	// -I /opt/intel/vtune_amplifier/include/ -L /opt/intel/vtune_amplifier/lib64 -ljitprofiling -ldl
	#include <jitprofiling.h>
	#ifdef _MSC_VER
		#pragma comment(lib, "libittnotify.lib")
	#endif
	#ifdef __linux__
		#include <dlfcn.h>
	#endif
#endif
#ifdef __linux__
	#define XBYAK_USE_PERF
#endif

namespace Xbyak { namespace util {

typedef enum {
   SmtLevel = 1,
   CoreLevel = 2
} IntelCpuTopologyLevel;

namespace local {

template<uint64_t L, uint64_t H = 0>
struct TypeT {
};

template<uint64_t L1, uint64_t H1, uint64_t L2, uint64_t H2>
TypeT<L1 | L2, H1 | H2> operator|(TypeT<L1, H1>, TypeT<L2, H2>) { return TypeT<L1 | L2, H1 | H2>(); }

} // local

/**
	CPU detection class
	@note static inline const member is supported by c++17 or later, so use template hack
*/
class Cpu {
public:
	class Type {
		uint64_t L;
		uint64_t H;
	public:
		Type(uint64_t L = 0, uint64_t H = 0) : L(L), H(H) { }
		template<uint64_t L_, uint64_t H_>
		Type(local::TypeT<L_, H_>) : L(L_), H(H_) {}
		Type& operator&=(const Type& rhs) { L &= rhs.L; H &= rhs.H; return *this; }
		Type& operator|=(const Type& rhs) { L |= rhs.L; H |= rhs.H; return *this; }
		Type operator&(const Type& rhs) const { Type t = *this; t &= rhs; return t; }
		Type operator|(const Type& rhs) const { Type t = *this; t |= rhs; return t; }
		bool operator==(const Type& rhs) const { return H == rhs.H && L == rhs.L; }
		bool operator!=(const Type& rhs) const { return !operator==(rhs); }
		// without explicit because backward compatilibity
		operator bool() const { return (H | L) != 0; }
		uint64_t getL() const { return L; }
		uint64_t getH() const { return H; }
	};
private:
	Type type_;
	//system topology
	bool x2APIC_supported_;
	static const size_t maxTopologyLevels = 2;
	uint32_t numCores_[maxTopologyLevels];

	static const uint32_t maxNumberCacheLevels = 10;
	uint32_t dataCacheSize_[maxNumberCacheLevels];
	uint32_t coresSharignDataCache_[maxNumberCacheLevels];
	uint32_t dataCacheLevels_;

	uint32_t get32bitAsBE(const char *x) const
	{
		return x[0] | (x[1] << 8) | (x[2] << 16) | (x[3] << 24);
	}
	uint32_t mask(int n) const
	{
		return (1U << n) - 1;
	}
	void setFamily()
	{
		uint32_t data[4] = {};
		getCpuid(1, data);
		stepping = data[0] & mask(4);
		model = (data[0] >> 4) & mask(4);
		family = (data[0] >> 8) & mask(4);
		// type = (data[0] >> 12) & mask(2);
		extModel = (data[0] >> 16) & mask(4);
		extFamily = (data[0] >> 20) & mask(8);
		if (family == 0x0f) {
			displayFamily = family + extFamily;
		} else {
			displayFamily = family;
		}
		if (family == 6 || family == 0x0f) {
			displayModel = (extModel << 4) + model;
		} else {
			displayModel = model;
		}
	}
	uint32_t extractBit(uint32_t val, uint32_t base, uint32_t end)
	{
		return (val >> base) & ((1u << (end - base)) - 1);
	}
	void setNumCores()
	{
		if (!has(tINTEL)) return;

		uint32_t data[4] = {};

		 /* CAUTION: These numbers are configuration as shipped by Intel. */
		getCpuidEx(0x0, 0, data);
		if (data[0] >= 0xB) {
			 /*
				if leaf 11 exists(x2APIC is supported),
				we use it to get the number of smt cores and cores on socket

				leaf 0xB can be zeroed-out by a hypervisor
			*/
			x2APIC_supported_ = true;
			for (uint32_t i = 0; i < maxTopologyLevels; i++) {
				getCpuidEx(0xB, i, data);
				IntelCpuTopologyLevel level = (IntelCpuTopologyLevel)extractBit(data[2], 8, 15);
				if (level == SmtLevel || level == CoreLevel) {
					numCores_[level - 1] = extractBit(data[1], 0, 15);
				}
			}
			/*
				Fallback values in case a hypervisor has 0xB leaf zeroed-out.
			*/
			numCores_[SmtLevel - 1] = (std::max)(1u, numCores_[SmtLevel - 1]);
			numCores_[CoreLevel - 1] = (std::max)(numCores_[SmtLevel - 1], numCores_[CoreLevel - 1]);
		} else {
			/*
				Failed to deremine num of cores without x2APIC support.
				TODO: USE initial APIC ID to determine ncores.
			*/
			numCores_[SmtLevel - 1] = 0;
			numCores_[CoreLevel - 1] = 0;
		}

	}
	void setCacheHierarchy()
	{
		if (!has(tINTEL)) return;
		const uint32_t NO_CACHE = 0;
		const uint32_t DATA_CACHE = 1;
//		const uint32_t INSTRUCTION_CACHE = 2;
		const uint32_t UNIFIED_CACHE = 3;
		uint32_t smt_width = 0;
		uint32_t logical_cores = 0;
		uint32_t data[4] = {};

		if (x2APIC_supported_) {
			smt_width = numCores_[0];
			logical_cores = numCores_[1];
		}

		/*
			Assumptions:
			the first level of data cache is not shared (which is the
			case for every existing architecture) and use this to
			determine the SMT width for arch not supporting leaf 11.
			when leaf 4 reports a number of core less than numCores_
			on socket reported by leaf 11, then it is a correct number
			of cores not an upperbound.
		*/
		for (int i = 0; dataCacheLevels_ < maxNumberCacheLevels; i++) {
			getCpuidEx(0x4, i, data);
			uint32_t cacheType = extractBit(data[0], 0, 4);
			if (cacheType == NO_CACHE) break;
			if (cacheType == DATA_CACHE || cacheType == UNIFIED_CACHE) {
				uint32_t actual_logical_cores = extractBit(data[0], 14, 25) + 1;
				if (logical_cores != 0) { // true only if leaf 0xB is supported and valid
					actual_logical_cores = (std::min)(actual_logical_cores, logical_cores);
				}
				assert(actual_logical_cores != 0);
				dataCacheSize_[dataCacheLevels_] =
					(extractBit(data[1], 22, 31) + 1)
					* (extractBit(data[1], 12, 21) + 1)
					* (extractBit(data[1], 0, 11) + 1)
					* (data[2] + 1);
				if (cacheType == DATA_CACHE && smt_width == 0) smt_width = actual_logical_cores;
				assert(smt_width != 0);
				coresSharignDataCache_[dataCacheLevels_] = (std::max)(actual_logical_cores / smt_width, 1u);
				dataCacheLevels_++;
			}
		}
	}

public:
	int model;
	int family;
	int stepping;
	int extModel;
	int extFamily;
	int displayFamily; // family + extFamily
	int displayModel; // model + extModel

	uint32_t getNumCores(IntelCpuTopologyLevel level) const {
		if (!x2APIC_supported_) XBYAK_THROW_RET(ERR_X2APIC_IS_NOT_SUPPORTED, 0)
		switch (level) {
		case SmtLevel: return numCores_[level - 1];
		case CoreLevel: return numCores_[level - 1] / numCores_[SmtLevel - 1];
		default: XBYAK_THROW_RET(ERR_X2APIC_IS_NOT_SUPPORTED, 0)
		}
	}

	uint32_t getDataCacheLevels() const { return dataCacheLevels_; }
	uint32_t getCoresSharingDataCache(uint32_t i) const
	{
		if (i >= dataCacheLevels_) XBYAK_THROW_RET(ERR_BAD_PARAMETER, 0)
		return coresSharignDataCache_[i];
	}
	uint32_t getDataCacheSize(uint32_t i) const
	{
		if (i >= dataCacheLevels_) XBYAK_THROW_RET(ERR_BAD_PARAMETER, 0)
		return dataCacheSize_[i];
	}

	/*
		data[] = { eax, ebx, ecx, edx }
	*/
	static inline void getCpuid(uint32_t eaxIn, uint32_t data[4])
	{
#ifdef XBYAK_INTEL_CPU_SPECIFIC
	#ifdef _WIN32
		__cpuid(reinterpret_cast<int*>(data), eaxIn);
	#else
		__cpuid(eaxIn, data[0], data[1], data[2], data[3]);
	#endif
#else
		(void)eaxIn;
		(void)data;
#endif
	}
	static inline void getCpuidEx(uint32_t eaxIn, uint32_t ecxIn, uint32_t data[4])
	{
#ifdef XBYAK_INTEL_CPU_SPECIFIC
	#ifdef _MSC_VER
		__cpuidex(reinterpret_cast<int*>(data), eaxIn, ecxIn);
	#else
		__cpuid_count(eaxIn, ecxIn, data[0], data[1], data[2], data[3]);
	#endif
#else
		(void)eaxIn;
		(void)ecxIn;
		(void)data;
#endif
	}
	static inline uint64_t getXfeature()
	{
#ifdef XBYAK_INTEL_CPU_SPECIFIC
	#ifdef _MSC_VER
		return _xgetbv(0);
	#else
		uint32_t eax, edx;
		// xgetvb is not support on gcc 4.2
//		__asm__ volatile("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
		__asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
		return ((uint64_t)edx << 32) | eax;
	#endif
#else
		return 0;
#endif
	}

#define XBYAK_SPLIT_ID(id) ((0 <= id && id < 64) ? (1ull << (id % 64)) : 0), (id >= 64 ? (1ull << (id % 64)) : 0)
#if (__cplusplus >= 201103) || (defined(_MSC_VER) && (_MSC_VER >= 1700)) /* VS2012 */
	#define XBYAK_DEFINE_TYPE(id, NAME) static const constexpr local::TypeT<XBYAK_SPLIT_ID(id)> NAME{}
#else
	#define XBYAK_DEFINE_TYPE(id, NAME) static const local::TypeT<XBYAK_SPLIT_ID(id)> NAME
#endif
	XBYAK_DEFINE_TYPE(0, tMMX);
	XBYAK_DEFINE_TYPE(1, tMMX2);
	XBYAK_DEFINE_TYPE(2, tCMOV);
	XBYAK_DEFINE_TYPE(3, tSSE);
	XBYAK_DEFINE_TYPE(4, tSSE2);
	XBYAK_DEFINE_TYPE(5, tSSE3);
	XBYAK_DEFINE_TYPE(6, tSSSE3);
	XBYAK_DEFINE_TYPE(7, tSSE41);
	XBYAK_DEFINE_TYPE(8, tSSE42);
	XBYAK_DEFINE_TYPE(9, tPOPCNT);
	XBYAK_DEFINE_TYPE(10, tAESNI);
	XBYAK_DEFINE_TYPE(11, tAVX512_FP16);
	XBYAK_DEFINE_TYPE(12, tOSXSAVE);
	XBYAK_DEFINE_TYPE(13, tPCLMULQDQ);
	XBYAK_DEFINE_TYPE(14, tAVX);
	XBYAK_DEFINE_TYPE(15, tFMA);
	XBYAK_DEFINE_TYPE(16, t3DN);
	XBYAK_DEFINE_TYPE(17, tE3DN);
	XBYAK_DEFINE_TYPE(18, tWAITPKG);
	XBYAK_DEFINE_TYPE(19, tRDTSCP);
	XBYAK_DEFINE_TYPE(20, tAVX2);
	XBYAK_DEFINE_TYPE(21, tBMI1); // andn, bextr, blsi, blsmsk, blsr, tzcnt
	XBYAK_DEFINE_TYPE(22, tBMI2); // bzhi, mulx, pdep, pext, rorx, sarx, shlx, shrx
	XBYAK_DEFINE_TYPE(23, tLZCNT);
	XBYAK_DEFINE_TYPE(24, tINTEL);
	XBYAK_DEFINE_TYPE(25, tAMD);
	XBYAK_DEFINE_TYPE(26, tENHANCED_REP); // enhanced rep movsb/stosb
	XBYAK_DEFINE_TYPE(27, tRDRAND);
	XBYAK_DEFINE_TYPE(28, tADX); // adcx, adox
	XBYAK_DEFINE_TYPE(29, tRDSEED); // rdseed
	XBYAK_DEFINE_TYPE(30, tSMAP); // stac
	XBYAK_DEFINE_TYPE(31, tHLE); // xacquire, xrelease, xtest
	XBYAK_DEFINE_TYPE(32, tRTM); // xbegin, xend, xabort
	XBYAK_DEFINE_TYPE(33, tF16C); // vcvtph2ps, vcvtps2ph
	XBYAK_DEFINE_TYPE(34, tMOVBE); // mobve
	XBYAK_DEFINE_TYPE(35, tAVX512F);
	XBYAK_DEFINE_TYPE(36, tAVX512DQ);
	XBYAK_DEFINE_TYPE(37, tAVX512_IFMA);
	XBYAK_DEFINE_TYPE(37, tAVX512IFMA);// = tAVX512_IFMA;
	XBYAK_DEFINE_TYPE(38, tAVX512PF);
	XBYAK_DEFINE_TYPE(39, tAVX512ER);
	XBYAK_DEFINE_TYPE(40, tAVX512CD);
	XBYAK_DEFINE_TYPE(41, tAVX512BW);
	XBYAK_DEFINE_TYPE(42, tAVX512VL);
	XBYAK_DEFINE_TYPE(43, tAVX512_VBMI);
	XBYAK_DEFINE_TYPE(43, tAVX512VBMI); // = tAVX512_VBMI; // changed by Intel's manual
	XBYAK_DEFINE_TYPE(44, tAVX512_4VNNIW);
	XBYAK_DEFINE_TYPE(45, tAVX512_4FMAPS);
	XBYAK_DEFINE_TYPE(46, tPREFETCHWT1);
	XBYAK_DEFINE_TYPE(47, tPREFETCHW);
	XBYAK_DEFINE_TYPE(48, tSHA);
	XBYAK_DEFINE_TYPE(49, tMPX);
	XBYAK_DEFINE_TYPE(50, tAVX512_VBMI2);
	XBYAK_DEFINE_TYPE(51, tGFNI);
	XBYAK_DEFINE_TYPE(52, tVAES);
	XBYAK_DEFINE_TYPE(53, tVPCLMULQDQ);
	XBYAK_DEFINE_TYPE(54, tAVX512_VNNI);
	XBYAK_DEFINE_TYPE(55, tAVX512_BITALG);
	XBYAK_DEFINE_TYPE(56, tAVX512_VPOPCNTDQ);
	XBYAK_DEFINE_TYPE(57, tAVX512_BF16);
	XBYAK_DEFINE_TYPE(58, tAVX512_VP2INTERSECT);
	XBYAK_DEFINE_TYPE(59, tAMX_TILE);
	XBYAK_DEFINE_TYPE(60, tAMX_INT8);
	XBYAK_DEFINE_TYPE(61, tAMX_BF16);
	XBYAK_DEFINE_TYPE(62, tAVX_VNNI);
	XBYAK_DEFINE_TYPE(63, tCLFLUSHOPT);
	XBYAK_DEFINE_TYPE(64, tCLDEMOTE);
	XBYAK_DEFINE_TYPE(65, tMOVDIRI);
	XBYAK_DEFINE_TYPE(66, tMOVDIR64B);
	XBYAK_DEFINE_TYPE(67, tCLZERO); // AMD Zen

#undef XBYAK_SPLIT_ID
#undef XBYAK_DEFINE_TYPE

	Cpu()
		: type_()
		, x2APIC_supported_(false)
		, numCores_()
		, dataCacheSize_()
		, coresSharignDataCache_()
		, dataCacheLevels_(0)
	{
		uint32_t data[4] = {};
		const uint32_t& EAX = data[0];
		const uint32_t& EBX = data[1];
		const uint32_t& ECX = data[2];
		const uint32_t& EDX = data[3];
		getCpuid(0, data);
		const uint32_t maxNum = EAX;
		static const char intel[] = "ntel";
		static const char amd[] = "cAMD";
		if (ECX == get32bitAsBE(amd)) {
			type_ |= tAMD;
			getCpuid(0x80000001, data);
			if (EDX & (1U << 31)) {
				type_ |= t3DN;
				// 3DNow! implies support for PREFETCHW on AMD
				type_ |= tPREFETCHW;
			}

			if (EDX & (1U << 29)) {
				// Long mode implies support for PREFETCHW on AMD
				type_ |= tPREFETCHW;
			}
		}
		if (ECX == get32bitAsBE(intel)) {
			type_ |= tINTEL;
		}

		// Extended flags information
		getCpuid(0x80000000, data);
		const uint32_t maxExtendedNum = EAX;
		if (maxExtendedNum >= 0x80000001) {
			getCpuid(0x80000001, data);

			if (EDX & (1U << 31)) type_ |= t3DN;
			if (EDX & (1U << 30)) type_ |= tE3DN;
			if (EDX & (1U << 27)) type_ |= tRDTSCP;
			if (EDX & (1U << 22)) type_ |= tMMX2;
			if (EDX & (1U << 15)) type_ |= tCMOV;
			if (ECX & (1U << 5)) type_ |= tLZCNT;
			if (ECX & (1U << 8)) type_ |= tPREFETCHW;
		}

		if (maxExtendedNum >= 0x80000008) {
			getCpuid(0x80000008, data);
			if (EBX & (1U << 0)) type_ |= tCLZERO;
		}

		getCpuid(1, data);
		if (ECX & (1U << 0)) type_ |= tSSE3;
		if (ECX & (1U << 9)) type_ |= tSSSE3;
		if (ECX & (1U << 19)) type_ |= tSSE41;
		if (ECX & (1U << 20)) type_ |= tSSE42;
		if (ECX & (1U << 22)) type_ |= tMOVBE;
		if (ECX & (1U << 23)) type_ |= tPOPCNT;
		if (ECX & (1U << 25)) type_ |= tAESNI;
		if (ECX & (1U << 1)) type_ |= tPCLMULQDQ;
		if (ECX & (1U << 27)) type_ |= tOSXSAVE;
		if (ECX & (1U << 30)) type_ |= tRDRAND;
		if (ECX & (1U << 29)) type_ |= tF16C;

		if (EDX & (1U << 15)) type_ |= tCMOV;
		if (EDX & (1U << 23)) type_ |= tMMX;
		if (EDX & (1U << 25)) type_ |= tMMX2 | tSSE;
		if (EDX & (1U << 26)) type_ |= tSSE2;

		if (type_ & tOSXSAVE) {
			// check XFEATURE_ENABLED_MASK[2:1] = '11b'
			uint64_t bv = getXfeature();
			if ((bv & 6) == 6) {
				if (ECX & (1U << 28)) type_ |= tAVX;
				if (ECX & (1U << 12)) type_ |= tFMA;
				// do *not* check AVX-512 state on macOS because it has on-demand AVX-512 support
#if !defined(__APPLE__)
				if (((bv >> 5) & 7) == 7)
#endif
				{
					getCpuidEx(7, 0, data);
					if (EBX & (1U << 16)) type_ |= tAVX512F;
					if (type_ & tAVX512F) {
						if (EBX & (1U << 17)) type_ |= tAVX512DQ;
						if (EBX & (1U << 21)) type_ |= tAVX512_IFMA;
						if (EBX & (1U << 26)) type_ |= tAVX512PF;
						if (EBX & (1U << 27)) type_ |= tAVX512ER;
						if (EBX & (1U << 28)) type_ |= tAVX512CD;
						if (EBX & (1U << 30)) type_ |= tAVX512BW;
						if (EBX & (1U << 31)) type_ |= tAVX512VL;
						if (ECX & (1U << 1)) type_ |= tAVX512_VBMI;
						if (ECX & (1U << 6)) type_ |= tAVX512_VBMI2;
						if (ECX & (1U << 11)) type_ |= tAVX512_VNNI;
						if (ECX & (1U << 12)) type_ |= tAVX512_BITALG;
						if (ECX & (1U << 14)) type_ |= tAVX512_VPOPCNTDQ;
						if (EDX & (1U << 2)) type_ |= tAVX512_4VNNIW;
						if (EDX & (1U << 3)) type_ |= tAVX512_4FMAPS;
						if (EDX & (1U << 8)) type_ |= tAVX512_VP2INTERSECT;
						if ((type_ & tAVX512BW) && (EDX & (1U << 23))) type_ |= tAVX512_FP16;
					}
				}
			}
		}
		if (maxNum >= 7) {
			getCpuidEx(7, 0, data);
			const uint32_t maxNumSubLeaves = EAX;
			if (type_ & tAVX && (EBX & (1U << 5))) type_ |= tAVX2;
			if (EBX & (1U << 3)) type_ |= tBMI1;
			if (EBX & (1U << 8)) type_ |= tBMI2;
			if (EBX & (1U << 9)) type_ |= tENHANCED_REP;
			if (EBX & (1U << 18)) type_ |= tRDSEED;
			if (EBX & (1U << 19)) type_ |= tADX;
			if (EBX & (1U << 20)) type_ |= tSMAP;
			if (EBX & (1U << 23)) type_ |= tCLFLUSHOPT;
			if (EBX & (1U << 4)) type_ |= tHLE;
			if (EBX & (1U << 11)) type_ |= tRTM;
			if (EBX & (1U << 14)) type_ |= tMPX;
			if (EBX & (1U << 29)) type_ |= tSHA;
			if (ECX & (1U << 0)) type_ |= tPREFETCHWT1;
			if (ECX & (1U << 5)) type_ |= tWAITPKG;
			if (ECX & (1U << 8)) type_ |= tGFNI;
			if (ECX & (1U << 9)) type_ |= tVAES;
			if (ECX & (1U << 10)) type_ |= tVPCLMULQDQ;
			if (ECX & (1U << 25)) type_ |= tCLDEMOTE;
			if (ECX & (1U << 27)) type_ |= tMOVDIRI;
			if (ECX & (1U << 28)) type_ |= tMOVDIR64B;
			if (EDX & (1U << 24)) type_ |= tAMX_TILE;
			if (EDX & (1U << 25)) type_ |= tAMX_INT8;
			if (EDX & (1U << 22)) type_ |= tAMX_BF16;
			if (maxNumSubLeaves >= 1) {
				getCpuidEx(7, 1, data);
				if (EAX & (1U << 4)) type_ |= tAVX_VNNI;
				if (type_ & tAVX512F) {
					if (EAX & (1U << 5)) type_ |= tAVX512_BF16;
				}
			}
		}
		setFamily();
		setNumCores();
		setCacheHierarchy();
	}
	void putFamily() const
	{
#ifndef XBYAK_ONLY_CLASS_CPU
		printf("family=%d, model=%X, stepping=%d, extFamily=%d, extModel=%X\n",
			family, model, stepping, extFamily, extModel);
		printf("display:family=%X, model=%X\n", displayFamily, displayModel);
#endif
	}
	bool has(const Type& type) const
	{
		return (type & type_) == type;
	}
};

#ifndef XBYAK_ONLY_CLASS_CPU
class Clock {
public:
	static inline uint64_t getRdtsc()
	{
#ifdef XBYAK_INTEL_CPU_SPECIFIC
	#ifdef _MSC_VER
		return __rdtsc();
	#else
		uint32_t eax, edx;
		__asm__ volatile("rdtsc" : "=a"(eax), "=d"(edx));
		return ((uint64_t)edx << 32) | eax;
	#endif
#else
		// TODO: Need another impl of Clock or rdtsc-equivalent for non-x86 cpu
		return 0;
#endif
	}
	Clock()
		: clock_(0)
		, count_(0)
	{
	}
	void begin()
	{
		clock_ -= getRdtsc();
	}
	void end()
	{
		clock_ += getRdtsc();
		count_++;
	}
	int getCount() const { return count_; }
	uint64_t getClock() const { return clock_; }
	void clear() { count_ = 0; clock_ = 0; }
private:
	uint64_t clock_;
	int count_;
};

#ifdef XBYAK64
const int UseRCX = 1 << 6;
const int UseRDX = 1 << 7;

class Pack {
	static const size_t maxTblNum = 15;
	Xbyak::Reg64 tbl_[maxTblNum];
	size_t n_;
public:
	Pack() : tbl_(), n_(0) {}
	Pack(const Xbyak::Reg64 *tbl, size_t n) { init(tbl, n); }
	Pack(const Pack& rhs)
		: n_(rhs.n_)
	{
		for (size_t i = 0; i < n_; i++) tbl_[i] = rhs.tbl_[i];
	}
	Pack& operator=(const Pack& rhs)
	{
		n_ = rhs.n_;
		for (size_t i = 0; i < n_; i++) tbl_[i] = rhs.tbl_[i];
		return *this;
	}
	Pack(const Xbyak::Reg64& t0)
	{ n_ = 1; tbl_[0] = t0; }
	Pack(const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 2; tbl_[0] = t0; tbl_[1] = t1; }
	Pack(const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 3; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; }
	Pack(const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 4; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; }
	Pack(const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 5; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; }
	Pack(const Xbyak::Reg64& t5, const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 6; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; tbl_[5] = t5; }
	Pack(const Xbyak::Reg64& t6, const Xbyak::Reg64& t5, const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 7; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; tbl_[5] = t5; tbl_[6] = t6; }
	Pack(const Xbyak::Reg64& t7, const Xbyak::Reg64& t6, const Xbyak::Reg64& t5, const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 8; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; tbl_[5] = t5; tbl_[6] = t6; tbl_[7] = t7; }
	Pack(const Xbyak::Reg64& t8, const Xbyak::Reg64& t7, const Xbyak::Reg64& t6, const Xbyak::Reg64& t5, const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 9; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; tbl_[5] = t5; tbl_[6] = t6; tbl_[7] = t7; tbl_[8] = t8; }
	Pack(const Xbyak::Reg64& t9, const Xbyak::Reg64& t8, const Xbyak::Reg64& t7, const Xbyak::Reg64& t6, const Xbyak::Reg64& t5, const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 10; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; tbl_[5] = t5; tbl_[6] = t6; tbl_[7] = t7; tbl_[8] = t8; tbl_[9] = t9; }
	Pack(const Xbyak::Reg64& ta, const Xbyak::Reg64& t9, const Xbyak::Reg64& t8, const Xbyak::Reg64& t7, const Xbyak::Reg64& t6, const Xbyak::Reg64& t5, const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 11; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; tbl_[5] = t5; tbl_[6] = t6; tbl_[7] = t7; tbl_[8] = t8; tbl_[9] = t9; tbl_[10] = ta; }
	Pack(const Xbyak::Reg64& tb, const Xbyak::Reg64& ta, const Xbyak::Reg64& t9, const Xbyak::Reg64& t8, const Xbyak::Reg64& t7, const Xbyak::Reg64& t6, const Xbyak::Reg64& t5, const Xbyak::Reg64& t4, const Xbyak::Reg64& t3, const Xbyak::Reg64& t2, const Xbyak::Reg64& t1, const Xbyak::Reg64& t0)
	{ n_ = 12; tbl_[0] = t0; tbl_[1] = t1; tbl_[2] = t2; tbl_[3] = t3; tbl_[4] = t4; tbl_[5] = t5; tbl_[6] = t6; tbl_[7] = t7; tbl_[8] = t8; tbl_[9] = t9; tbl_[10] = ta; tbl_[11] = tb; }
	Pack& append(const Xbyak::Reg64& t)
	{
		if (n_ == maxTblNum) {
			fprintf(stderr, "ERR Pack::can't append\n");
			XBYAK_THROW_RET(ERR_BAD_PARAMETER, *this)
		}
		tbl_[n_++] = t;
		return *this;
	}
	void init(const Xbyak::Reg64 *tbl, size_t n)
	{
		if (n > maxTblNum) {
			fprintf(stderr, "ERR Pack::init bad n=%d\n", (int)n);
			XBYAK_THROW(ERR_BAD_PARAMETER)
		}
		n_ = n;
		for (size_t i = 0; i < n; i++) {
			tbl_[i] = tbl[i];
		}
	}
	const Xbyak::Reg64& operator[](size_t n) const
	{
		if (n >= n_) {
			fprintf(stderr, "ERR Pack bad n=%d(%d)\n", (int)n, (int)n_);
			XBYAK_THROW_RET(ERR_BAD_PARAMETER, rax)
		}
		return tbl_[n];
	}
	size_t size() const { return n_; }
	/*
		get tbl[pos, pos + num)
	*/
	Pack sub(size_t pos, size_t num = size_t(-1)) const
	{
		if (num == size_t(-1)) num = n_ - pos;
		if (pos + num > n_) {
			fprintf(stderr, "ERR Pack::sub bad pos=%d, num=%d\n", (int)pos, (int)num);
			XBYAK_THROW_RET(ERR_BAD_PARAMETER, Pack())
		}
		Pack pack;
		pack.n_ = num;
		for (size_t i = 0; i < num; i++) {
			pack.tbl_[i] = tbl_[pos + i];
		}
		return pack;
	}
	void put() const
	{
		for (size_t i = 0; i < n_; i++) {
			printf("%s ", tbl_[i].toString());
		}
		printf("\n");
	}
};

class StackFrame {
#ifdef XBYAK64_WIN
	static const int noSaveNum = 6;
	static const int rcxPos = 0;
	static const int rdxPos = 1;
#else
	static const int noSaveNum = 8;
	static const int rcxPos = 3;
	static const int rdxPos = 2;
#endif
	static const int maxRegNum = 14; // maxRegNum = 16 - rsp - rax
	Xbyak::CodeGenerator *code_;
	int pNum_;
	int tNum_;
	bool useRcx_;
	bool useRdx_;
	int saveNum_;
	int P_;
	bool makeEpilog_;
	Xbyak::Reg64 pTbl_[4];
	Xbyak::Reg64 tTbl_[maxRegNum];
	Pack p_;
	Pack t_;
	StackFrame(const StackFrame&);
	void operator=(const StackFrame&);
public:
	const Pack& p;
	const Pack& t;
	/*
		make stack frame
		@param sf [in] this
		@param pNum [in] num of function parameter(0 <= pNum <= 4)
		@param tNum [in] num of temporary register(0 <= tNum, with UseRCX, UseRDX) #{pNum + tNum [+rcx] + [rdx]} <= 14
		@param stackSizeByte [in] local stack size
		@param makeEpilog [in] automatically call close() if true

		you can use
		rax
		gp0, ..., gp(pNum - 1)
		gt0, ..., gt(tNum-1)
		rcx if tNum & UseRCX
		rdx if tNum & UseRDX
		rsp[0..stackSizeByte - 1]
	*/
	StackFrame(Xbyak::CodeGenerator *code, int pNum, int tNum = 0, int stackSizeByte = 0, bool makeEpilog = true)
		: code_(code)
		, pNum_(pNum)
		, tNum_(tNum & ~(UseRCX | UseRDX))
		, useRcx_((tNum & UseRCX) != 0)
		, useRdx_((tNum & UseRDX) != 0)
		, saveNum_(0)
		, P_(0)
		, makeEpilog_(makeEpilog)
		, p(p_)
		, t(t_)
	{
		using namespace Xbyak;
		if (pNum < 0 || pNum > 4) XBYAK_THROW(ERR_BAD_PNUM)
		const int allRegNum = pNum + tNum_ + (useRcx_ ? 1 : 0) + (useRdx_ ? 1 : 0);
		if (tNum_ < 0 || allRegNum > maxRegNum) XBYAK_THROW(ERR_BAD_TNUM)
		const Reg64& _rsp = code->rsp;
		saveNum_ = (std::max)(0, allRegNum - noSaveNum);
		const int *tbl = getOrderTbl() + noSaveNum;
		for (int i = 0; i < saveNum_; i++) {
			code->push(Reg64(tbl[i]));
		}
		P_ = (stackSizeByte + 7) / 8;
		if (P_ > 0 && (P_ & 1) == (saveNum_ & 1)) P_++; // (rsp % 16) == 8, then increment P_ for 16 byte alignment
		P_ *= 8;
		if (P_ > 0) code->sub(_rsp, P_);
		int pos = 0;
		for (int i = 0; i < pNum; i++) {
			pTbl_[i] = Xbyak::Reg64(getRegIdx(pos));
		}
		for (int i = 0; i < tNum_; i++) {
			tTbl_[i] = Xbyak::Reg64(getRegIdx(pos));
		}
		if (useRcx_ && rcxPos < pNum) code_->mov(code_->r10, code_->rcx);
		if (useRdx_ && rdxPos < pNum) code_->mov(code_->r11, code_->rdx);
		p_.init(pTbl_, pNum);
		t_.init(tTbl_, tNum_);
	}
	/*
		make epilog manually
		@param callRet [in] call ret() if true
	*/
	void close(bool callRet = true)
	{
		using namespace Xbyak;
		const Reg64& _rsp = code_->rsp;
		const int *tbl = getOrderTbl() + noSaveNum;
		if (P_ > 0) code_->add(_rsp, P_);
		for (int i = 0; i < saveNum_; i++) {
			code_->pop(Reg64(tbl[saveNum_ - 1 - i]));
		}

		if (callRet) code_->ret();
	}
	~StackFrame()
	{
		if (!makeEpilog_) return;
		close();
	}
private:
	const int *getOrderTbl() const
	{
		using namespace Xbyak;
		static const int tbl[] = {
#ifdef XBYAK64_WIN
			Operand::RCX, Operand::RDX, Operand::R8, Operand::R9, Operand::R10, Operand::R11, Operand::RDI, Operand::RSI,
#else
			Operand::RDI, Operand::RSI, Operand::RDX, Operand::RCX, Operand::R8, Operand::R9, Operand::R10, Operand::R11,
#endif
			Operand::RBX, Operand::RBP, Operand::R12, Operand::R13, Operand::R14, Operand::R15
		};
		return &tbl[0];
	}
	int getRegIdx(int& pos) const
	{
		assert(pos < maxRegNum);
		using namespace Xbyak;
		const int *tbl = getOrderTbl();
		int r = tbl[pos++];
		if (useRcx_) {
			if (r == Operand::RCX) { return Operand::R10; }
			if (r == Operand::R10) { r = tbl[pos++]; }
		}
		if (useRdx_) {
			if (r == Operand::RDX) { return Operand::R11; }
			if (r == Operand::R11) { return tbl[pos++]; }
		}
		return r;
	}
};
#endif

class Profiler {
	int mode_;
	const char *suffix_;
	const void *startAddr_;
#ifdef XBYAK_USE_PERF
	FILE *fp_;
#endif
public:
	enum {
		None = 0,
		Perf = 1,
		VTune = 2
	};
	Profiler()
		: mode_(None)
		, suffix_("")
		, startAddr_(0)
#ifdef XBYAK_USE_PERF
		, fp_(0)
#endif
	{
	}
	// append suffix to funcName
	void setNameSuffix(const char *suffix)
	{
		suffix_ = suffix;
	}
	void setStartAddr(const void *startAddr)
	{
		startAddr_ = startAddr;
	}
	void init(int mode)
	{
		mode_ = None;
		switch (mode) {
		default:
		case None:
			return;
		case Perf:
#ifdef XBYAK_USE_PERF
			close();
			{
				const int pid = getpid();
				char name[128];
				snprintf(name, sizeof(name), "/tmp/perf-%d.map", pid);
				fp_ = fopen(name, "a+");
				if (fp_ == 0) {
					fprintf(stderr, "can't open %s\n", name);
					return;
				}
			}
			mode_ = Perf;
#endif
			return;
		case VTune:
#ifdef XBYAK_USE_VTUNE
			dlopen("dummy", RTLD_LAZY); // force to load dlopen to enable jit profiling
			if (iJIT_IsProfilingActive() != iJIT_SAMPLING_ON) {
				fprintf(stderr, "VTune profiling is not active\n");
				return;
			}
			mode_ = VTune;
#endif
			return;
		}
	}
	~Profiler()
	{
		close();
	}
	void close()
	{
#ifdef XBYAK_USE_PERF
		if (fp_ == 0) return;
		fclose(fp_);
		fp_ = 0;
#endif
	}
	void set(const char *funcName, const void *startAddr, size_t funcSize) const
	{
		if (mode_ == None) return;
#if !defined(XBYAK_USE_PERF) && !defined(XBYAK_USE_VTUNE)
		(void)funcName;
		(void)startAddr;
		(void)funcSize;
#endif
#ifdef XBYAK_USE_PERF
		if (mode_ == Perf) {
			if (fp_ == 0) return;
			fprintf(fp_, "%llx %zx %s%s", (long long)startAddr, funcSize, funcName, suffix_);
			/*
				perf does not recognize the function name which is less than 3,
				so append '_' at the end of the name if necessary
			*/
			size_t n = strlen(funcName) + strlen(suffix_);
			for (size_t i = n; i < 3; i++) {
				fprintf(fp_, "_");
			}
			fprintf(fp_, "\n");
			fflush(fp_);
		}
#endif
#ifdef XBYAK_USE_VTUNE
		if (mode_ != VTune) return;
		char className[] = "";
		char fileName[] = "";
		iJIT_Method_Load jmethod = {};
		jmethod.method_id = iJIT_GetNewMethodID();
		jmethod.class_file_name = className;
		jmethod.source_file_name = fileName;
		jmethod.method_load_address = const_cast<void*>(startAddr);
		jmethod.method_size = funcSize;
		jmethod.line_number_size = 0;
		char buf[128];
		snprintf(buf, sizeof(buf), "%s%s", funcName, suffix_);
		jmethod.method_name = buf;
		iJIT_NotifyEvent(iJVM_EVENT_TYPE_METHOD_LOAD_FINISHED, (void*)&jmethod);
#endif
	}
	/*
		for continuous set
		funcSize = endAddr - <previous set endAddr>
	*/
	void set(const char *funcName, const void *endAddr)
	{
		set(funcName, startAddr_, (size_t)endAddr - (size_t)startAddr_);
		startAddr_ = endAddr;
	}
};
#endif // XBYAK_ONLY_CLASS_CPU

} } // end of util

#endif
