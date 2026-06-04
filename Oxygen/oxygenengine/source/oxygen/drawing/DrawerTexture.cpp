/*
*	Part of the Oxygen Engine / Sonic 3 A.I.R. software distribution.
*	Copyright (C) 2017-2026 by Eukaryot
*
*	Published under the GNU GPLv3 open source software license, see license.txt
*	or https://www.gnu.org/licenses/gpl-3.0.en.html
*/

#include "oxygen/pch.h"
#include "oxygen/drawing/DrawerTexture.h"
#include "oxygen/drawing/Drawer.h"


void DrawerTextureImplementation::updateFromBitmapRegion(const Bitmap& bitmap, const Recti& rect)
{
	updateFromBitmap(bitmap);
}

void DrawerTextureImplementation::updateFromBitmapRegions(const Bitmap& bitmap, const std::vector<Recti>& rects)
{
	for (const Recti& rect : rects)
	{
		updateFromBitmapRegion(bitmap, rect);
	}
}

DrawerTexture::~DrawerTexture()
{
	invalidate();

	// Unregister
	if (nullptr != mRegisteredOwner)
	{
		mRegisteredOwner->unregisterTexture(*this);
	}
}

void DrawerTexture::invalidate()
{
	delete mImplementation;
	mImplementation = nullptr;
}

void DrawerTexture::ensureValidity()
{
	if (nullptr == mImplementation)
	{
		EngineMain::instance().getDrawer().createTexture(*this);
	}
}

void DrawerTexture::setImplementation(DrawerTextureImplementation* implementation)
{
	delete mImplementation;
	mImplementation = implementation;

	if (nullptr != implementation)
	{
		implementation->setContentKnownOpaque(mContentKnownOpaque);
		implementation->refreshImplementation(mSetupAsRenderTarget, mSize);
	}
}

void DrawerTexture::clearBitmap()
{
	mBitmap.clear();
	invalidate();
}

const Bitmap& DrawerTexture::getBitmap() const
{
	return mBitmap;
}

Bitmap& DrawerTexture::accessBitmap()
{
	return mBitmap;
}

void DrawerTexture::setContentKnownOpaque(bool knownOpaque)
{
	mContentKnownOpaque = knownOpaque;
	if (nullptr != mImplementation)
	{
		mImplementation->setContentKnownOpaque(knownOpaque);
	}
}

void DrawerTexture::bitmapUpdated()
{
	mSize.set(mBitmap.getWidth(), mBitmap.getHeight());
	mSetupAsRenderTarget = false;

	ensureValidity();
	if (nullptr != mImplementation)
	{
		mImplementation->updateFromBitmap(mBitmap);
	}
}

void DrawerTexture::bitmapRegionUpdated(const Recti& rect)
{
	mSize.set(mBitmap.getWidth(), mBitmap.getHeight());
	mSetupAsRenderTarget = false;

	if (rect.empty())
		return;

	ensureValidity();
	if (nullptr != mImplementation)
	{
		mImplementation->updateFromBitmapRegion(mBitmap, rect);
	}
}

void DrawerTexture::bitmapRegionsUpdated(const std::vector<Recti>& rects)
{
	mSize.set(mBitmap.getWidth(), mBitmap.getHeight());
	mSetupAsRenderTarget = false;

	bool hasRegion = false;
	for (const Recti& rect : rects)
	{
		if (!rect.empty())
		{
			hasRegion = true;
			break;
		}
	}
	if (!hasRegion)
		return;

	ensureValidity();
	if (nullptr != mImplementation)
	{
		mImplementation->updateFromBitmapRegions(mBitmap, rects);
	}
}

void DrawerTexture::setupAsRenderTarget(const Vec2i& size)
{
	// Any change?
	if (mSetupAsRenderTarget && mSize == size)
		return;

	mSize = size;
	mSetupAsRenderTarget = true;

	ensureValidity();
	if (nullptr != mImplementation)
	{
		mImplementation->setupAsRenderTarget(mSize);
	}
}

void DrawerTexture::writeContentToBitmap(Bitmap& outBitmap)
{
	ensureValidity();
	if (nullptr != mImplementation)
	{
		mImplementation->writeContentToBitmap(outBitmap);
	}
}

void DrawerTexture::swap(DrawerTexture& other)
{
	mBitmap.swap(other.mBitmap);
	std::swap(mSize, other.mSize);
	std::swap(mContentKnownOpaque, other.mContentKnownOpaque);
	std::swap(mImplementation, other.mImplementation);
}
