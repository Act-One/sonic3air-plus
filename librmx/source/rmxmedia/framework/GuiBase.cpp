/*
*	rmx Library
*	Copyright (C) 2008-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "rmxmedia.h"

#if defined(PLATFORM_WIIU)
	#include <proc_ui/procui.h>
#endif


#if defined(PLATFORM_WIIU)
namespace
{
	bool isProcUIExitTeardownActive()
	{
		if (ProcUIIsRunning() && ProcUIInShutdown())
			return true;
		return nullptr != FTX::System && FTX::System->isWiiUProcUIExitRequested();
	}
}
#endif


GuiBase::GuiBase()
{
}

GuiBase::~GuiBase()
{
	deleteAllChildren();
	if (nullptr != mParent)
		mParent->removeChild(*this);
}

void GuiBase::addChild(GuiBase& child)
{
	GuiBase* oldParent = child.mParent;
	if (oldParent)
	{
		if (oldParent == this)
			return;
		oldParent->removeChild(child);
	}

	mChildren.push_back(&child);
	child.mParent = this;
	child.initialize();
}

void GuiBase::removeChild(GuiBase& child)
{
	if (child.mParent != this)
		return;

#if defined(PLATFORM_WIIU)
	if (isProcUIExitTeardownActive())
	{
		child.mParent = nullptr;
		if (mIteratingChildren)
		{
			mChildrenToRemove.push_back(&child);
		}
		else
		{
			internalRemoveChild(child);
		}
		return;
	}
#endif

	child.deinitialize();
	child.mParent = nullptr;

	if (mIteratingChildren)
	{
		mChildrenToRemove.push_back(&child);
	}
	else
	{
		internalRemoveChild(child);
	}
}

void GuiBase::deleteChild(GuiBase& child)
{
	removeChild(child);
	delete &child;
}

void GuiBase::removeAllChildren()
{
#if defined(PLATFORM_WIIU)
	if (isProcUIExitTeardownActive())
	{
		for (GuiBase* child : mChildren)
		{
			if (nullptr != child)
				child->mParent = nullptr;
		}
		mChildren.clear();
		mChildrenToRemove.clear();
		mIteratingChildren = false;
		return;
	}
#endif

	for (GuiBase* child : mChildren)
	{
		child->deinitialize();
		child->mParent = nullptr;
	}
	mChildren.clear();
}

void GuiBase::deleteAllChildren()
{
#if defined(PLATFORM_WIIU)
	if (isProcUIExitTeardownActive())
	{
		static uint32 sDetachLogCount = 0;
		if (!mChildren.empty() && sDetachLogCount < 8)
		{
			++sDetachLogCount;
			RMX_LOG_INFO("GuiBase: detaching " << (uint32)mChildren.size() << " GUI children during ProcUI exit teardown");
		}
		for (GuiBase* child : mChildren)
		{
			if (nullptr != child)
				child->mParent = nullptr;
		}
		mChildren.clear();
		mChildrenToRemove.clear();
		mIteratingChildren = false;
		return;
	}
#endif
	for (GuiBase* child : mChildren)
	{
		child->deinitialize();
		child->mParent = nullptr;
		delete child;
	}
	mChildren.clear();
}

void GuiBase::moveToFront(GuiBase& child)
{
	RMX_ASSERT(child.mParent == this, "Element to move to front is not a child");
	if (mChildren.back() == &child)		// Child in front of all others is the last one in the child list
		return;

	internalRemoveChild(child);
	mChildren.push_back(&child);
}

void GuiBase::moveToBack(GuiBase& child)
{
	RMX_ASSERT(child.mParent == this, "Element to move to back is not a child");
	if (mChildren.front() == &child)	// Child behind all others is the first one in the child list
		return;

	internalRemoveChild(child);
	mChildren.insert(mChildren.begin(), &child);
}

void GuiBase::removeFromParent()
{
	if (nullptr != mParent)
		mParent->removeChild(*this);
}

void GuiBase::initialize()
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->initialize();
	}
	endIteratingChildren();
}

void GuiBase::deinitialize()
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->deinitialize();
	}
	endIteratingChildren();
}

void GuiBase::beginFrame()
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->beginFrame();
	}
	endIteratingChildren();
}

void GuiBase::endFrame()
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->endFrame();
	}
	endIteratingChildren();
}

void GuiBase::sdlEvent(const SDL_Event& ev)
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->sdlEvent(ev);
	}
	endIteratingChildren();
}

void GuiBase::mouse(const rmx::MouseEvent& ev)
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->mouse(ev);
	}
	endIteratingChildren();
}

void GuiBase::keyboard(const rmx::KeyboardEvent& ev)
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->keyboard(ev);
	}
	endIteratingChildren();
}

void GuiBase::textinput(const rmx::TextInputEvent& ev)
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->textinput(ev);
	}
	endIteratingChildren();
}

void GuiBase::update(float deltaSeconds)
{
	beginIteratingChildren();
	for (int k = (int)mChildren.size() - 1; k >= 0; --k)	// Iterate in reverse order
	{
		mChildren[k]->update(deltaSeconds);
	}
	endIteratingChildren();
}

void GuiBase::render()
{
	beginIteratingChildren();
	for (int k = 0; k < (int)mChildren.size(); ++k)			// Iterate in forward order
	{
		mChildren[k]->render();
	}
	endIteratingChildren();
}

void GuiBase::internalRemoveChild(GuiBase& child)
{
	for (auto it = mChildren.begin(); it != mChildren.end(); ++it)
	{
		if (*it == &child)
		{
			mChildren.erase(it);
			break;
		}
	}
}

void GuiBase::beginIteratingChildren()
{
	mIteratingChildren = true;
}

void GuiBase::endIteratingChildren()
{
	mIteratingChildren = false;

	for (GuiBase* child : mChildrenToRemove)
	{
		internalRemoveChild(*child);
	}
	mChildrenToRemove.clear();
}
