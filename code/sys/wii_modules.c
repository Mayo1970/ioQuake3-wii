/* Team Arena's cgame, qagame and ui, linked into the DOL by the Makefile (WII_NATIVE_TA).
   Mods and non-stock pure-server QVMs still go through the interpreter. */
#if defined(WII_NATIVE_TA)

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#include <stdlib.h>
#include <string.h>

typedef void (*wiiDllEntry_t)(intptr_t (QDECL *syscallptr)(intptr_t, ...));

#define WII_MODULE_DECLARE(mod) \
	intptr_t vmMain_##mod(int command, int arg0, int arg1, int arg2, int arg3, int arg4, \
		int arg5, int arg6, int arg7, int arg8, int arg9, int arg10, int arg11); \
	void dllEntry_##mod(intptr_t (QDECL *syscallptr)(intptr_t, ...)); \
	extern char wiimod_##mod##_data_start[], wiimod_##mod##_data_end[]; \
	extern char wiimod_##mod##_bss_start[], wiimod_##mod##_bss_end[]

WII_MODULE_DECLARE(qagame);
WII_MODULE_DECLARE(cgame);
WII_MODULE_DECLARE(ui);

typedef struct
{
	const char *name;
	vmMainProc vmMain;
	wiiDllEntry_t dllEntry;
	char *dataStart;
	char *dataEnd;
	char *bssStart;
	char *bssEnd;
	void *pristineData;
	qboolean active;
	qboolean failed;
} wiiModule_t;

#define WII_MODULE(mod) \
	{ #mod, vmMain_##mod, dllEntry_##mod, \
		wiimod_##mod##_data_start, wiimod_##mod##_data_end, \
		wiimod_##mod##_bss_start, wiimod_##mod##_bss_end, NULL, qfalse, qfalse }

static wiiModule_t wiiModules[] =
{
	WII_MODULE(qagame),
	WII_MODULE(cgame),
	WII_MODULE(ui),
};

static wiiModule_t *Sys_WiiFindModule(const char *name)
{
	int i;

	for (i = 0; i < (int)ARRAY_LEN(wiiModules); i++)
	{
		if (!Q_stricmp(name, wiiModules[i].name))
			return &wiiModules[i];
	}
	return NULL;
}

qboolean Sys_WiiUseBuiltinModule(const char *name)
{
	wiiModule_t *module = Sys_WiiFindModule(name);

	return module && !module->failed && !Q_stricmp(FS_GetCurrentGameDir(), BASETA);
}

/* A QVM starts from fresh globals; restore .data and zero .bss to match. */
static qboolean Sys_WiiResetModule(wiiModule_t *module)
{
	size_t dataSize = (size_t)(module->dataEnd - module->dataStart);
	size_t bssSize = (size_t)(module->bssEnd - module->bssStart);

	if (!module->pristineData)
	{
		/* First load: the module has never run, so .data is still the linked image. */
		module->pristineData = malloc(dataSize ? dataSize : 1);
		if (!module->pristineData)
		{
			Com_Printf("Wii module %s: no memory for the %d byte data snapshot\n",
				module->name, (int)dataSize);
			return qfalse;
		}
		memcpy(module->pristineData, module->dataStart, dataSize);
		Com_Printf("Wii module %s: data %d KiB, bss %d KiB\n", module->name,
			(int)(dataSize / 1024), (int)(bssSize / 1024));
		return qtrue;
	}

	memcpy(module->dataStart, module->pristineData, dataSize);
	memset(module->bssStart, 0, bssSize);
	return qtrue;
}

void *QDECL Sys_LoadGameDll(const char *name, vmMainProc *entryPoint,
	intptr_t (QDECL *systemcalls)(intptr_t, ...))
{
	wiiModule_t *module = Sys_WiiFindModule(name);

	if (!module)
		return NULL;
	if (module->active)
	{
		Com_Printf("Wii module %s: already loaded\n", name);
		return NULL;
	}
	/* Leave *entryPoint untouched on failure so vm.c can fall back to the QVM. */
	if (!Sys_WiiResetModule(module))
	{
		module->failed = qtrue;
		return NULL;
	}

	module->active = qtrue;
	*entryPoint = module->vmMain;
	Com_Printf("Wii module %s: native\n", module->name);
	module->dllEntry(systemcalls);
	return module;
}

void Sys_UnloadDll(void *handle)
{
	wiiModule_t *module = handle;

	if (module >= wiiModules && module < wiiModules + ARRAY_LEN(wiiModules))
		module->active = qfalse;
}

#else  /* !WII_NATIVE_TA */

typedef int wii_modules_placeholder;

#endif /* WII_NATIVE_TA */
