// SPDX-License-Identifier: MIT
// Application-local adaptation of nxdk lib/hal/audio.c (004fefbfe8fd93a5e).
// Xemu's emulated STAC9700 codec advances analog DMA, but its SPDIF channel is
// not consumed. Use analog completion for that codec only; retain nxdk's dual
// output synchronization on a real Xbox. This file intentionally overrides
// the static SDK archive's XAudio symbols without modifying the shared SDK.

// SPDX-FileCopyrightText: 2003 Andy Green
// SPDX-FileCopyrightText: 2004 Craig Edwards
// SPDX-FileCopyrightText: 2005-2006 Richard Osborne
// SPDX-FileCopyrightText: 2006 Guillaume Lamonoca
// SPDX-FileCopyrightText: 2017-2020 Jannik Vogel
// SPDX-FileCopyrightText: 2020-2021 Stefan Schmidt

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <hal/audio.h>
#include <xboxkrnl/xboxkrnl.h>

// The foundation for this file came from the Cromwell audio driver by 
// Andy (see his comments below).

// Andy@warmcat.com 2003-03-10:
//
// Xbox PC audio is an AC97 compatible audio controller in the MCPX chip
// http://www.amd.com/us-en/assets/content_type/white_papers_and_tech_docs/24467.pdf
// unlike standard AC97 all the regs appear as MMIO from 0xfec00000
// +0 - +7f = Mixer/Codec regs  <-- Wolfson Micro chip
// +100 - +17f = Busmaster regs

// The Wolfson Micro 9709 Codec fitted to the Xbox is a "dumb" codec that
//  has NO PC-controllable registers.  The only regs it has are the
//  manufacturer and device ID ones.

// S/PDIF comes out of the MCPX device and is controlled by an extra set
//  of busmaster DMA registers at +0x170.  These need their own descriptor
//  separate from the PCM Out, although that descriptor can share audio
//  buffers with PCM out successfully.

#define AUDIO_IRQ 6

static volatile unsigned int analogBufferCount;
static volatile unsigned int digitalBufferCount;
static bool analogDrained;
static bool digitalDrained;
static bool analogCompletionOnly;

static KINTERRUPT InterruptObject;
static KDPC DPCObject;

// global reference to the ac97 device
AC97_DEVICE ac97Device;


//DPCs avoid crashes inside non reentrant user callbacks called by nested ISRs.
//CAUTION : if you use fpu in DPC you have to save & restore yourself fpu state!!!
//(fpu=floating point unit, i.e the coprocessor executing floating point opcodes)
static void __stdcall DPC(PKDPC Dpc, 
					PVOID DeferredContext, 
					PVOID SystemArgument1, 
					PVOID SystemArgument2)
{
	AC97_DEVICE *pac97device;

	pac97device = &ac97Device;
	if (pac97device)
		if (pac97device->callback)
			(pac97device->callback)((void *)pac97device, pac97device->callbackData);

	return;
}
	
// Although we have to explicitly clear the S/PDIF interrupt sources, in fact
// the way we are set up PCM and S/PDIF are in lockstep and we only listen for
// PCM actions, since S/PDIF is always spooling through the same buffer.
static BOOLEAN __stdcall ISR(PKINTERRUPT Interrupt, PVOID ServiceContext)
{
	AC97_DEVICE *pac97device = &ac97Device;
	volatile unsigned char *pb = (unsigned char *)pac97device->mmio;

	unsigned char analogInterrupt = pb[0x116];
	unsigned char digitalInterrupt = pb[0x176];

	bool waitCompleted = false;

	if (analogInterrupt) {
		bool waitingForAnalog = analogCompletionOnly || (analogBufferCount > digitalBufferCount);

		// Was the interrupt triggered because we were out of data?
		if ((analogInterrupt & 8) && (analogCompletionOnly || !analogDrained)) {
			if (analogBufferCount > 0) {
				waitCompleted |= waitingForAnalog;
				analogBufferCount--;
			}
		}

        if (analogCompletionOnly) {
            // No emulated SPDIF consumer exists to catch up with analog.
            digitalBufferCount = analogBufferCount;
        }
		analogDrained = analogInterrupt & 4;

		pb[0x116]=0xFF; // clear all int sources
	}

	if (digitalInterrupt) {
		bool waitingForDigital = !analogCompletionOnly && (digitalBufferCount > analogBufferCount);

		// Was the interrupt triggered because we were out of data?
		if ((digitalInterrupt & 8) && !digitalDrained) {
			if (digitalBufferCount > 0) {
				waitCompleted |= waitingForDigital;
				digitalBufferCount--;
			}
		}

		digitalDrained = digitalInterrupt & 4;

		pb[0x176]=0xFF; // clear all int sources
	}


	// If a buffer was consumed by analog and digital output, we ask for a DPC
	if (waitCompleted) {
		//KeInsertQueueDpc queues Dpc and returns TRUE if Dpc not already queued.
		//Dpc will be queued only once. So only one Dpc is fired after ISRs cease fire.
		//DPCs avoid crashes inside non reentrant user callbacks called by nested ISRs.
		//CAUTION : if you use fpu in DPC you have to save & restore yourself fpu state!!!
		//(fpu=floating point unit, i.e the coprocessor executing floating point opcodes)
		KeInsertQueueDpc(&DPCObject,NULL,NULL); //calls user callback soon
	}

	return TRUE;
}


void XDumpAudioStatus(void)
{
	volatile AC97_DEVICE *pac97device = &ac97Device;
	if (pac97device)
	{
		volatile unsigned char *pb = (unsigned char *)pac97device->mmio;
		//debugPrint("CIV=%02x LVI=%02x SR=%04x CR=%02x\n", pb[0x114], pb[0x115], pb[0x116], pb[0x11B]);
	}
}

// Initialises the audio subsystem.  This *must* be done before
// audio will work.  You can pass NULL as the callback, but if you
// do that, it is your responsibility to keep feeding the data to
// XAudioProvideSamples() manually.
//
// note that I currently ignore sampleSizeInBits and numChannels.  They
// are provided to cope with future enhancements. Currently supported samples
// are 16 bits, 2 channels (stereo)
void XAudioInit(int sampleSizeInBits, int numChannels, XAudioCallback callback, void *data)
{
	AC97_DEVICE * pac97device = &ac97Device;
	KIRQL irql;
	ULONG vector;

	// Hack to prevent an assertion in MmGetPhysicalAddress by locking the memory.
	// A future API redesign should use proper allocation
	// (MmAllocateContiguousMemory) instead.
	MmLockUnlockBufferPages((PVOID)pac97device, sizeof(AC97_DEVICE), FALSE);

	pac97device->mmio = (unsigned int *)0xfec00000;
	pac97device->nextDescriptor = 0;
	pac97device->callback = callback;
	pac97device->callbackData = data;
	pac97device->sampleSizeInBits = sampleSizeInBits;
	pac97device->numChannels = numChannels;

	volatile unsigned char *pb = (unsigned char *)pac97device->mmio;
    volatile uint16_t *codec = (volatile uint16_t *)pb;
    analogCompletionOnly = codec[0x7c / 2] == 0x8384 && codec[0x7e / 2] == 0x7600;

	// initialise descriptors to all zero (no samples)
	for(unsigned int i = 0; i < 32; i++) {
		pac97device->pcmSpdifDescriptor[i].bufferStartAddress = 0;
		pac97device->pcmSpdifDescriptor[i].bufferLengthInSamples = 0;
		pac97device->pcmSpdifDescriptor[i].bufferControl = 0;
		pac97device->pcmOutDescriptor[i].bufferStartAddress = 0;
		pac97device->pcmOutDescriptor[i].bufferLengthInSamples = 0;
		pac97device->pcmOutDescriptor[i].bufferControl = 0;
	}

	// perform cold reset
	pac97device->mmio[0x12C>>2] &= ~2;
	LARGE_INTEGER Interval;
	Interval.QuadPart = -10;
	KeDelayExecutionThread(KernelMode, FALSE, &Interval);
	pac97device->mmio[0x12C>>2] |= 2;
	
	// wait until the chip is finished resetting...
	while(!(pac97device->mmio[0x130>>2]&0x100))
		;

	// reset bus master registers for analog output
	pb[0x11B] = (1 << 4) | (1 << 3) | (1 << 2) | (1 << 1);
	while(pb[0x11B] & (1 << 1))
		;

	// reset bus master registers for digital output
	pb[0x17B] = (1 << 4) | (1 << 3) | (1 << 2) | (1 << 1);
	while(pb[0x17B] & (1 << 1))
		;

	// clear all interrupts
	pb[0x116] = 0xFF;
	pb[0x176] = 0xFF;

	// tell the audio chip where it should look for the descriptors
	unsigned int pcmAddress = (unsigned int)&pac97device->pcmOutDescriptor[0];
	unsigned int spdifAddress = (unsigned int)&pac97device->pcmSpdifDescriptor[0];
	pac97device->mmio[0x100>>2] = 0;  // no PCM input
	pac97device->mmio[0x110>>2] = MmGetPhysicalAddress((void *)pcmAddress);
	pac97device->mmio[0x170>>2] = MmGetPhysicalAddress((void *)spdifAddress);

	// default to being silent...
	XAudioPause();

	// reset buffer status
	analogBufferCount = 0;
	digitalBufferCount = 0;
	analogDrained = false;
	digitalDrained = false;
	
	// Register our ISR
	vector = HalGetInterruptVector(AUDIO_IRQ, &irql);

	KeInitializeDpc(&DPCObject,&DPC,NULL);

	KeInitializeInterrupt(&InterruptObject,
				&ISR,
				NULL,
				vector,
				irql,
				LevelSensitive,
				FALSE);
	
	KeConnectInterrupt(&InterruptObject);
}

// tell the chip it is OK to play...
void XAudioPlay(void)
{
	AC97_DEVICE *pac97device = &ac97Device;
	volatile unsigned char *pb = (unsigned char *)pac97device->mmio;
	pb[0x11B] = 0x1d; // PCM out - run, allow interrupts
	pb[0x17B] = 0x1d; // PCM out - run, allow interrupts
}

// tell the chip it is paused.
void XAudioPause(void)
{
	AC97_DEVICE *pac97device = &ac97Device;
	volatile unsigned char *pb = (unsigned char *)pac97device->mmio;
	pb[0x11B] = 0x1c; // PCM out - PAUSE, allow interrupts
	pb[0x17B] = 0x1c; // PCM out - PAUSE, allow interrupts
}

// This is the function you should call when you want to give the
// audio chip some more data.  If you have registered a callback, it
// should call this method.  If you are providing the samples manually,
// you need to make sure you call this function often enough so the
// chip doesn't run out of data
typedef struct AUDIO_SUBMISSION {
    unsigned int address;
    unsigned int wordCount;
    unsigned short bufferControl;
} AUDIO_SUBMISSION;

/* KeSynchronizeExecution invokes this synchronously at the audio interrupt's
 * synchronization IRQL. DMA may finish during publication, but ISR accounting
 * cannot run until both channels, their counts and nextDescriptor agree.
 * Only bounded stores occur here: no allocation, blocking, callbacks or FPU.
 * In particular, publishing LVI can immediately resume a drained DMA engine.
 * Its completion must not be acknowledged before the new count is recorded. */
static BOOLEAN __stdcall PublishSamples(PVOID context)
{
    const AUDIO_SUBMISSION *submission = context;
	AC97_DEVICE *pac97device = &ac97Device;
	volatile unsigned char *pb = (unsigned char *)pac97device->mmio;

    unsigned short bufferControl = submission->bufferControl;
    unsigned int address = submission->address;
    unsigned int wordCount = submission->wordCount;

	pac97device->pcmOutDescriptor[pac97device->nextDescriptor].bufferStartAddress    = address;
	pac97device->pcmOutDescriptor[pac97device->nextDescriptor].bufferLengthInSamples = wordCount;
	pac97device->pcmOutDescriptor[pac97device->nextDescriptor].bufferControl         = bufferControl;
	analogBufferCount++;

	pac97device->pcmSpdifDescriptor[pac97device->nextDescriptor].bufferStartAddress    = address;
	pac97device->pcmSpdifDescriptor[pac97device->nextDescriptor].bufferLengthInSamples = wordCount;
	pac97device->pcmSpdifDescriptor[pac97device->nextDescriptor].bufferControl         = bufferControl;
	digitalBufferCount++;

    unsigned char descriptor = pac97device->nextDescriptor;
	// increment to the next buffer descriptor (rolling around to 0 once you get to 31)
	pac97device->nextDescriptor = (pac97device->nextDescriptor + 1) % 32;
    /* Publish only completely initialized descriptors and bookkeeping. */
    __asm__ volatile ("" ::: "memory");
    pb[0x115] = descriptor;
    pb[0x175] = descriptor;
    return TRUE;
}

void XAudioProvideSamples(unsigned char *buffer, unsigned short bufferLength, int isFinal)
{
    AUDIO_SUBMISSION submission;
    submission.address = MmGetPhysicalAddress((PVOID)buffer);
    submission.wordCount = bufferLength / 2;
    submission.bufferControl = 0x8000 | (isFinal ? 0x4000 : 0);

    /* The stack context cannot escape: this call completes PublishSamples
     * before returning. XAudioInit has connected InterruptObject before the
     * backend's first submission; no concurrently reinitializing caller is
     * supported by the existing XAudio API. */
    KeSynchronizeExecution(&InterruptObject, PublishSamples, &submission);
}

/* Snapshot analog frames still pending in the SDL backend's 1024-frame DMA
 * buffers. The current descriptor's PICB counts 16-bit channel samples. Retry
 * a descriptor transition, without disabling interrupts or modifying DMA. */
unsigned rampage_audio_pending_frames(void)
{
    volatile unsigned char *pb = (unsigned char *)ac97Device.mmio;
    if (!pb) return 0;
    unsigned count = 0, left = 0;
    for (int tries = 0; tries < 3; ++tries) {
        count = analogBufferCount;
        unsigned civ = pb[0x114];
        left = *(volatile unsigned short *)(pb + 0x118) / 2;
        if (count == analogBufferCount && civ == pb[0x114]) break;
    }
    if (!count) return 0;
    if (count > 2) count = 2; /* SDL backend owns two buffers */
    if (left > 1024) left = 1024;
    return (count - 1) * 1024 + left;
}
