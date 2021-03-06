/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <windows.h>

#include "nsMathUtils.h"

#include "gfxWindowsNativeDrawing.h"
#include "gfxWindowsSurface.h"
#include "gfxAlphaRecovery.h"
#include "gfxPattern.h"

enum {
    RENDER_STATE_INIT,

    RENDER_STATE_NATIVE_DRAWING,
    RENDER_STATE_NATIVE_DRAWING_DONE,

    RENDER_STATE_ALPHA_RECOVERY_BLACK,
    RENDER_STATE_ALPHA_RECOVERY_BLACK_DONE,
    RENDER_STATE_ALPHA_RECOVERY_WHITE,
    RENDER_STATE_ALPHA_RECOVERY_WHITE_DONE,

    RENDER_STATE_DONE
};

gfxWindowsNativeDrawing::gfxWindowsNativeDrawing(gfxContext* ctx,
                                                 const gfxRect& nativeRect,
                                                 PRUint32 nativeDrawFlags)
    : mContext(ctx), mNativeRect(nativeRect), mNativeDrawFlags(nativeDrawFlags), mRenderState(RENDER_STATE_INIT)
{
}

HDC
gfxWindowsNativeDrawing::BeginNativeDrawing()
{
    if (mRenderState == RENDER_STATE_INIT) {
        nsRefPtr<gfxASurface> surf;
        
        if (mContext->GetCairo()) {
          surf = mContext->CurrentSurface(&mDeviceOffset.x, &mDeviceOffset.y);
        }

        if (surf && surf->CairoStatus())
            return nsnull;

        gfxMatrix m = mContext->CurrentMatrix();
        if (!m.HasNonTranslation())
            mTransformType = TRANSLATION_ONLY;
        else if (m.HasNonAxisAlignedTransform())
            mTransformType = COMPLEX;
        else
            mTransformType = AXIS_ALIGNED_SCALE;

        // if this is a native win32 surface, we don't have to
        // redirect rendering to our own HDC; in some cases,
        // we may be able to use the HDC from the surface directly.
        if (surf &&
            ((surf->GetType() == gfxASurface::SurfaceTypeWin32 ||
              surf->GetType() == gfxASurface::SurfaceTypeWin32Printing) &&
              (surf->GetContentType() == gfxASurface::CONTENT_COLOR ||
               (surf->GetContentType() == gfxASurface::CONTENT_COLOR_ALPHA &&
               (mNativeDrawFlags & CAN_DRAW_TO_COLOR_ALPHA)))))
        {
            // grab the DC. This can fail if there is a complex clipping path,
            // in which case we'll have to fall back.
            mWinSurface = static_cast<gfxWindowsSurface*>(static_cast<gfxASurface*>(surf.get()));
            mDC = mWinSurface->GetDCWithClip(mContext);

            if (mDC) {
                if (mTransformType == TRANSLATION_ONLY) {
                    mRenderState = RENDER_STATE_NATIVE_DRAWING;

                    mTranslation = m.GetTranslation();
                } else if (((mTransformType == AXIS_ALIGNED_SCALE)
                            && (mNativeDrawFlags & CAN_AXIS_ALIGNED_SCALE)) ||
                           (mNativeDrawFlags & CAN_COMPLEX_TRANSFORM))
                {
                    mWorldTransform.eM11 = (FLOAT) m.xx;
                    mWorldTransform.eM12 = (FLOAT) m.yx;
                    mWorldTransform.eM21 = (FLOAT) m.xy;
                    mWorldTransform.eM22 = (FLOAT) m.yy;
                    mWorldTransform.eDx  = (FLOAT) m.x0;
                    mWorldTransform.eDy  = (FLOAT) m.y0;

                    mRenderState = RENDER_STATE_NATIVE_DRAWING;
                }
            }
        }

        // If we couldn't do native drawing, then we have to do two-buffer drawing
        // and do alpha recovery
        if (mRenderState == RENDER_STATE_INIT) {
            mRenderState = RENDER_STATE_ALPHA_RECOVERY_BLACK;

            // We round out our native rect here, that way the snapping will
            // happen correctly.
            mNativeRect.RoundOut();

            // we only do the scale bit if we can do an axis aligned
            // scale; otherwise we scale (if necessary) after
            // rendering with cairo.  Note that if we're doing alpha recovery,
            // we cannot do a full complex transform with win32 (I mean, we could, but
            // it would require more code that's not here.)
            if (mTransformType == TRANSLATION_ONLY || !(mNativeDrawFlags & CAN_AXIS_ALIGNED_SCALE)) {
                mScale = gfxSize(1.0, 1.0);

                // Add 1 to the surface size; it's guaranteed to not be incorrect,
                // and it fixes bug 382458
                // There's probably a better fix, but I haven't figured out
                // the root cause of the problem.
                mTempSurfaceSize =
                    gfxIntSize((PRInt32) ceil(mNativeRect.Width() + 1),
                               (PRInt32) ceil(mNativeRect.Height() + 1));
            } else {
                // figure out the scale factors
                mScale = m.ScaleFactors(true);

                mWorldTransform.eM11 = (FLOAT) mScale.width;
                mWorldTransform.eM12 = 0.0f;
                mWorldTransform.eM21 = 0.0f;
                mWorldTransform.eM22 = (FLOAT) mScale.height;
                mWorldTransform.eDx  = 0.0f;
                mWorldTransform.eDy  = 0.0f;

                // See comment above about "+1"
                mTempSurfaceSize =
                    gfxIntSize((PRInt32) ceil(mNativeRect.Width() * mScale.width + 1),
                               (PRInt32) ceil(mNativeRect.Height() * mScale.height + 1));
            }
        }
    }

    if (mRenderState == RENDER_STATE_NATIVE_DRAWING) {
        // we can just do native drawing directly to the context's surface

        // do we need to use SetWorldTransform?
        if (mTransformType != TRANSLATION_ONLY) {
            SetGraphicsMode(mDC, GM_ADVANCED);
            GetWorldTransform(mDC, &mOldWorldTransform);
            SetWorldTransform(mDC, &mWorldTransform);
        }
        GetViewportOrgEx(mDC, &mOrigViewportOrigin);
        SetViewportOrgEx(mDC,
                         mOrigViewportOrigin.x + (int)mDeviceOffset.x,
                         mOrigViewportOrigin.y + (int)mDeviceOffset.y,
                         NULL);

        return mDC;
    } else if (mRenderState == RENDER_STATE_ALPHA_RECOVERY_BLACK ||
               mRenderState == RENDER_STATE_ALPHA_RECOVERY_WHITE)
    {
        // we're going to use mWinSurface to create our temporary surface here

        // get us a RGB24 DIB; DIB is important, because
        // we can later call GetImageSurface on it.
        mWinSurface = new gfxWindowsSurface(mTempSurfaceSize);
        mDC = mWinSurface->GetDC();

        RECT r = { 0, 0, mTempSurfaceSize.width, mTempSurfaceSize.height };
        if (mRenderState == RENDER_STATE_ALPHA_RECOVERY_BLACK)
            FillRect(mDC, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        else
            FillRect(mDC, &r, (HBRUSH)GetStockObject(WHITE_BRUSH));

        if ((mTransformType != TRANSLATION_ONLY) &&
            (mNativeDrawFlags & CAN_AXIS_ALIGNED_SCALE))
        {
            SetGraphicsMode(mDC, GM_ADVANCED);
            SetWorldTransform(mDC, &mWorldTransform);
        }

        return mDC;
    } else {
        NS_ERROR("Bogus render state!");
        return nsnull;
    }
}

bool
gfxWindowsNativeDrawing::IsDoublePass()
{
    if (!mContext->IsCairo()) {
      return true;
    }

    nsRefPtr<gfxASurface> surf = mContext->CurrentSurface(&mDeviceOffset.x, &mDeviceOffset.y);
    if (!surf || surf->CairoStatus())
        return false;
    if (surf->GetType() != gfxASurface::SurfaceTypeWin32 &&
        surf->GetType() != gfxASurface::SurfaceTypeWin32Printing) {
	return true;
    }
    if ((surf->GetContentType() != gfxASurface::CONTENT_COLOR ||
         (surf->GetContentType() == gfxASurface::CONTENT_COLOR_ALPHA &&
          !(mNativeDrawFlags & CAN_DRAW_TO_COLOR_ALPHA))))
        return true;
    return false;
}

bool
gfxWindowsNativeDrawing::ShouldRenderAgain()
{
    switch (mRenderState) {
        case RENDER_STATE_NATIVE_DRAWING_DONE:
            return false;

        case RENDER_STATE_ALPHA_RECOVERY_BLACK_DONE:
            mRenderState = RENDER_STATE_ALPHA_RECOVERY_WHITE;
            return true;

        case RENDER_STATE_ALPHA_RECOVERY_WHITE_DONE:
            return false;

        default:
            NS_ERROR("Invalid RenderState in gfxWindowsNativeDrawing::ShouldRenderAgain");
            break;
    }

    return false;
}

void
gfxWindowsNativeDrawing::EndNativeDrawing()
{
    if (mRenderState == RENDER_STATE_NATIVE_DRAWING) {
        // we drew directly to the HDC in the context; undo our changes
        SetViewportOrgEx(mDC, mOrigViewportOrigin.x, mOrigViewportOrigin.y, NULL);

        if (mTransformType != TRANSLATION_ONLY)
            SetWorldTransform(mDC, &mOldWorldTransform);

        mWinSurface->MarkDirty();

        mRenderState = RENDER_STATE_NATIVE_DRAWING_DONE;
    } else if (mRenderState == RENDER_STATE_ALPHA_RECOVERY_BLACK) {
        mBlackSurface = mWinSurface;
        mWinSurface = nsnull;

        mRenderState = RENDER_STATE_ALPHA_RECOVERY_BLACK_DONE;
    } else if (mRenderState == RENDER_STATE_ALPHA_RECOVERY_WHITE) {
        mWhiteSurface = mWinSurface;
        mWinSurface = nsnull;

        mRenderState = RENDER_STATE_ALPHA_RECOVERY_WHITE_DONE;
    } else {
        NS_ERROR("Invalid RenderState in gfxWindowsNativeDrawing::EndNativeDrawing");
    }
}

void
gfxWindowsNativeDrawing::PaintToContext()
{
    if (mRenderState == RENDER_STATE_NATIVE_DRAWING_DONE) {
        // nothing to do, it already went to the context
        mRenderState = RENDER_STATE_DONE;
    } else if (mRenderState == RENDER_STATE_ALPHA_RECOVERY_WHITE_DONE) {
        nsRefPtr<gfxImageSurface> black = mBlackSurface->GetAsImageSurface();
        nsRefPtr<gfxImageSurface> white = mWhiteSurface->GetAsImageSurface();
        if (!gfxAlphaRecovery::RecoverAlpha(black, white)) {
            NS_ERROR("Alpha recovery failure");
            return;
        }
        nsRefPtr<gfxImageSurface> alphaSurface =
            new gfxImageSurface(black->Data(), black->GetSize(),
                                black->Stride(),
                                gfxASurface::ImageFormatARGB32);

        mContext->Save();
        mContext->Translate(mNativeRect.TopLeft());
        mContext->NewPath();
        mContext->Rectangle(gfxRect(gfxPoint(0.0, 0.0), mNativeRect.Size()));

        nsRefPtr<gfxPattern> pat = new gfxPattern(alphaSurface);

        gfxMatrix m;
        m.Scale(mScale.width, mScale.height);
        pat->SetMatrix(m);

        if (mNativeDrawFlags & DO_NEAREST_NEIGHBOR_FILTERING)
            pat->SetFilter(gfxPattern::FILTER_FAST);

        pat->SetExtend(gfxPattern::EXTEND_PAD);
        mContext->SetPattern(pat);
        mContext->Fill();
        mContext->Restore();

        mRenderState = RENDER_STATE_DONE;
    } else {
        NS_ERROR("Invalid RenderState in gfxWindowsNativeDrawing::PaintToContext");
    }
}

void
gfxWindowsNativeDrawing::TransformToNativeRect(const gfxRect& r,
                                               RECT& rout)
{
    /* If we're doing native drawing, then we're still in the coordinate space
     * of the context; otherwise, we're in our own little world,
     * relative to the passed-in nativeRect.
     */

    gfxRect roundedRect(r);

    if (mRenderState == RENDER_STATE_NATIVE_DRAWING) {
        if (mTransformType == TRANSLATION_ONLY) {
            roundedRect.MoveBy(mTranslation);
        }
    } else {
        roundedRect.MoveBy(-mNativeRect.TopLeft());
    }

    roundedRect.Round();

    rout.left   = LONG(roundedRect.X());
    rout.right  = LONG(roundedRect.XMost());
    rout.top    = LONG(roundedRect.Y());
    rout.bottom = LONG(roundedRect.YMost());
}
