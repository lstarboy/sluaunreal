// Tencent is pleased to support the open source community by making sluaunreal available.

// Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
// Licensed under the BSD 3-Clause License (the "License"); 
// you may not use this file except in compliance with the License. You may obtain a copy of the License at

// https://opensource.org/licenses/BSD-3-Clause

// Unless required by applicable law or agreed to in writing, 
// software distributed under the License is distributed on an "AS IS" BASIS, 
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
// See the License for the specific language governing permissions and limitations under the License.

#pragma once
#define LUA_LIB
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "LuaVar.h"
#include <string>
#include <memory>
#include "HAL/Runnable.h"

#define SLUA_LUACODE "[sluacode]"
#define SLUA_CPPINST "__cppinst"

namespace slua {

	struct ScriptTimeoutEvent {
		virtual void onTimeout() = 0;
	};

	class FDeadLoopCheck : public FRunnable
	{
	public:
		FDeadLoopCheck();
		~FDeadLoopCheck();

		void scriptEnter(ScriptTimeoutEvent* pEvent);
		void scriptLeave();

	protected:
		uint32 Run() override;
		void Stop() override;
		void onScriptTimeout();
	private:
		ScriptTimeoutEvent* timeoutEvent;
		FThreadSafeCounter timeoutCounter;
		FThreadSafeCounter stopCounter;
		FThreadSafeCounter frameCounter;
		FRunnableThread* thread;
	};

	// check lua script dead loop
	class LuaScriptCallGuard : public ScriptTimeoutEvent {
	public:
		LuaScriptCallGuard(lua_State* L);
		virtual ~LuaScriptCallGuard();
		void onTimeout() override;
	private:
		lua_State* L;
		static void scriptTimeout(lua_State *L, lua_Debug *ar);
	};

    class SLUA_UNREAL_API LuaState
    {
    public:
        LuaState(const char* name=nullptr);
        virtual ~LuaState();

        /*
         * fn, lua file to load, fn may be a short filename
         * if find fn to load, return file size to len and file full path fo filepath arguments
         * if find fn and load successful, return buf of file content, otherwise return nullptr
         * you must delete[] buf returned by function for free memory.
         */
        typedef uint8* (*LoadFileDelegate) (const char* fn, uint32& len, FString& filepath);

        inline static LuaState* get(lua_State* l=nullptr) {
            // if L is nullptr, return main state
            if(!l) return mainState;
            return (LuaState*)*((void**)lua_getextraspace(l));
        }
        // get LuaState from state index
        static LuaState* get(int index);

        // get LuaState from name
        static LuaState* get(const FString& name);

        // return specified index is valid state index
        inline static bool isValid(int index)  {
            return get(index)!=nullptr;
        }
        
        // return state index
        int stateIndex() const { return si; }
        
        // init lua state
        virtual bool init();
        // tick function
        virtual void tick(float dtime);
        // close lua state
        virtual void close();

        // execute lua string
        LuaVar doString(const char* str, LuaVar* pEnv = nullptr);
        // execute bytes buffer and named buffer to chunk
        LuaVar doBuffer(const uint8* buf,uint32 len, const char* chunk, LuaVar* pEnv = nullptr);
        // load file and execute it
        // file how to loading depend on load delegation
        // see setLoadFileDelegate function
        LuaVar doFile(const char* fn, LuaVar* pEnv = nullptr);

       
        // call function that specified by key
        // any supported c++ value can be passed as argument to lua 
        template<typename ...ARGS>
        LuaVar call(const char* key,ARGS&& ...args) {
            LuaVar f = get(key);
            return f.call(std::forward<ARGS>(args)...);
        }

        // get field from _G, support "x.x.x.x" to search children field
        LuaVar get(const char* key);
		// set field to _G, support "x.x.x.x" to create sub table recursive
		bool set(const char* key, LuaVar v);

        // set load delegation function to load lua code
		void setLoadFileDelegate(LoadFileDelegate func);

		lua_State* getLuaState() const
		{
			return L;
		}
		operator lua_State*() const
		{
			return L;
		}

        // create a empty table
		LuaVar createTable();
		// create named table, support "x.x.x.x", put table to _G
		LuaVar createTable(const char* key);


		void addRef(UObject* obj) {
			ensure(root);
			root->AddRef(obj);

			UClass* objClass = obj->GetClass();
			int32* instanceNumPtr = classInstanceNums.Find(objClass);
			if (!instanceNumPtr)
			{
				instanceNumPtr = &classInstanceNums.Add(objClass, 0);
			}

			(*instanceNumPtr)++;
		}

		void removeRef(UObject* obj) {
			// if obj had been free by UE, return directly
			if (!obj->IsValidLowLevelFast()) return;
			UClass* objClass = obj->GetClass();
			int32* instanceNumPtr = classInstanceNums.Find(objClass);
            ensure(instanceNumPtr);
			(*instanceNumPtr)--;
			if (*instanceNumPtr == 0)
			{
				classInstanceNums.Remove(objClass);

				auto classFunctionsPtr = classMap.Find(objClass);
				if (classFunctionsPtr)
				{
					delete *classFunctionsPtr;
					classMap.Remove(objClass);
				}
			}

			ensure(root);
			root->Remove(obj);
		}



		const TMap<UObject*, UObject*>& cacheMap() {
			return root->Cache;
		}
        
        static int pushErrorHandler(lua_State* L);
    protected:
        LoadFileDelegate loadFileDelegate;
        uint8* loadFile(const char* fn,uint32& len,FString& filepath);
		static int loader(lua_State* L);
		static int getStringFromMD5(lua_State* L);
    private:
        friend class LuaObject;
        friend class SluaUtil;
		friend struct LuaEnums;
		friend class LuaScriptCallGuard;
        lua_State* L;
        int cacheObjRef;
		// init enums lua code
        int _pushErrorHandler(lua_State* L);
        static int _atPanic(lua_State* L);
		void linkProp(void* parent, void* prop);
		void releaseLink(void* prop);
		void releaseAllLink();
		void onGameEndPlay();

		TMap<void*, TArray<void*>> propLinks;
        ULuaObject* root;
        int stackCount;
        int si;
        FString stateName;

        TMap<UClass*,TMap<FString,UFunction*>*> classMap;

		TMap<UClass*, int32> classInstanceNums;

		FDeadLoopCheck* deadLoopCheck;

		FDelegateHandle handlerForEndPlay;

        static LuaState* mainState;

        #if WITH_EDITOR
        // used for debug
		TMap<FString, FString> debugStringMap;
        #endif
    };
}