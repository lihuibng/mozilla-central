/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef __NS_SVGOUTERSVGFRAME_H__
#define __NS_SVGOUTERSVGFRAME_H__

#include "gfxMatrix.h"
#include "nsISVGSVGFrame.h"
#include "nsSVGContainerFrame.h"

class nsSVGForeignObjectFrame;

////////////////////////////////////////////////////////////////////////
// nsSVGOuterSVGFrame class

typedef nsSVGDisplayContainerFrame nsSVGOuterSVGFrameBase;

class nsSVGOuterSVGFrame : public nsSVGOuterSVGFrameBase,
                           public nsISVGSVGFrame
{
  friend nsIFrame*
  NS_NewSVGOuterSVGFrame(nsIPresShell* aPresShell, nsStyleContext* aContext);
protected:
  nsSVGOuterSVGFrame(nsStyleContext* aContext);

public:
  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS

#ifdef DEBUG
  ~nsSVGOuterSVGFrame() {
    NS_ASSERTION(mForeignObjectHash.Count() == 0,
                 "foreignObject(s) still registered!");
  }
#endif

  // nsIFrame:
  virtual nscoord GetMinWidth(nsRenderingContext *aRenderingContext);
  virtual nscoord GetPrefWidth(nsRenderingContext *aRenderingContext);

  virtual IntrinsicSize GetIntrinsicSize();
  virtual nsSize  GetIntrinsicRatio();

  virtual nsSize ComputeSize(nsRenderingContext *aRenderingContext,
                             nsSize aCBSize, nscoord aAvailableWidth,
                             nsSize aMargin, nsSize aBorder, nsSize aPadding,
                             PRUint32 aFlags) MOZ_OVERRIDE;

  NS_IMETHOD Reflow(nsPresContext*          aPresContext,
                    nsHTMLReflowMetrics&     aDesiredSize,
                    const nsHTMLReflowState& aReflowState,
                    nsReflowStatus&          aStatus);

  NS_IMETHOD  DidReflow(nsPresContext*   aPresContext,
                        const nsHTMLReflowState*  aReflowState,
                        nsDidReflowStatus aStatus);

  NS_IMETHOD BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                              const nsRect&           aDirtyRect,
                              const nsDisplayListSet& aLists);

  NS_IMETHOD Init(nsIContent*      aContent,
                  nsIFrame*        aParent,
                  nsIFrame*        aPrevInFlow);

  virtual nsSplittableType GetSplittableType() const;

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::svgOuterSVGFrame
   */
  virtual nsIAtom* GetType() const;

#ifdef DEBUG
  NS_IMETHOD GetFrameName(nsAString& aResult) const
  {
    return MakeFrameName(NS_LITERAL_STRING("SVGOuterSVG"), aResult);
  }
#endif

  NS_IMETHOD  AttributeChanged(PRInt32         aNameSpaceID,
                               nsIAtom*        aAttribute,
                               PRInt32         aModType);

  virtual nsIFrame* GetContentInsertionFrame() {
    // Any children must be added to our single anonymous inner frame kid.
    NS_ABORT_IF_FALSE(GetFirstPrincipalChild() &&
                      GetFirstPrincipalChild()->GetType() ==
                        nsGkAtoms::svgOuterSVGAnonChildFrame,
                      "Where is our anonymous child?");
    return GetFirstPrincipalChild()->GetContentInsertionFrame();
  }

  virtual bool IsSVGTransformed(gfxMatrix *aOwnTransform,
                                gfxMatrix *aFromParentTransform) const {
    // Outer-<svg> can transform its children with viewBox, currentScale and
    // currentTranslate, but it itself is not transformed by SVG transforms.
    return false;
  }

  // nsISVGSVGFrame interface:
  virtual void NotifyViewportOrTransformChanged(PRUint32 aFlags);

  // nsISVGChildFrame methods:
  NS_IMETHOD PaintSVG(nsRenderingContext* aContext,
                      const nsIntRect *aDirtyRect);

  virtual SVGBBox GetBBoxContribution(const gfxMatrix &aToBBoxUserspace,
                                      PRUint32 aFlags);

  // nsSVGContainerFrame methods:
  virtual gfxMatrix GetCanvasTM(PRUint32 aFor);

  /* Methods to allow descendant nsSVGForeignObjectFrame frames to register and
   * unregister themselves with their nearest nsSVGOuterSVGFrame ancestor. This
   * is temporary until display list based invalidation is impleented for SVG.
   * Maintaining a list of our foreignObject descendants allows us to search
   * them for areas that need to be invalidated, without having to also search
   * the SVG frame tree for foreignObjects. This is important so that bug 539356
   * does not slow down SVG in general (only foreignObjects, until bug 614732 is
   * fixed).
   */
  void RegisterForeignObject(nsSVGForeignObjectFrame* aFrame);
  void UnregisterForeignObject(nsSVGForeignObjectFrame* aFrame);

  virtual bool HasChildrenOnlyTransform(gfxMatrix *aTransform) const {
    // Our anonymous wrapper child must claim our children-only transforms as
    // its own so that our real children (the frames it wraps) are transformed
    // by them, and we must pretend we don't have any children-only transforms
    // so that our anonymous child is _not_ transformed by them.
    return false;
  }

  /**
   * Return true only if the height is unspecified (defaulting to 100%) or else
   * the height is explicitly set to a percentage value no greater than 100%.
   */
  bool VerticalScrollbarNotNeeded() const;

  bool IsCallingReflowSVG() const {
    return mCallingReflowSVG;
  }

protected:

  bool mCallingReflowSVG;

  /* Returns true if our content is the document element and our document is
   * embedded in an HTML 'object', 'embed' or 'applet' element. Set
   * aEmbeddingFrame to obtain the nsIFrame for the embedding HTML element.
   */
  bool IsRootOfReplacedElementSubDoc(nsIFrame **aEmbeddingFrame = nsnull);

  /* Returns true if our content is the document element and our document is
   * being used as an image.
   */
  bool IsRootOfImage();

  // This is temporary until display list based invalidation is implemented for
  // SVG.
  // A hash-set containing our nsSVGForeignObjectFrame descendants. Note we use
  // a hash-set to avoid the O(N^2) behavior we'd get tearing down an SVG frame
  // subtree if we were to use a list (see bug 381285 comment 20).
  nsTHashtable<nsVoidPtrHashKey> mForeignObjectHash;

  nsAutoPtr<gfxMatrix> mCanvasTM;

  float mFullZoom;

  bool mViewportInitialized;
  bool mIsRootContent;
};

////////////////////////////////////////////////////////////////////////
// nsSVGOuterSVGAnonChildFrame class

typedef nsSVGDisplayContainerFrame nsSVGOuterSVGAnonChildFrameBase;

/**
 * nsSVGOuterSVGFrames have a single direct child that is an instance of this
 * class, and which is used to wrap their real child frames. Such anonymous
 * wrapper frames created from this class exist because SVG frames need their
 * GetPosition() offset to be their offset relative to "user space" (in app
 * units) so that they can play nicely with nsDisplayTransform. This is fine
 * for all SVG frames except for direct children of an nsSVGOuterSVGFrame,
 * since an nsSVGOuterSVGFrame can have CSS border and padding (unlike other
 * SVG frames). The direct children can't include the offsets due to any such
 * border/padding in their mRects since that would break nsDisplayTransform,
 * but not including these offsets would break other parts of the Mozilla code
 * that assume a frame's mRect contains its border-box-to-parent-border-box
 * offset, in particular nsIFrame::GetOffsetTo and the functions that depend on
 * it. Wrapping an nsSVGOuterSVGFrame's children in an instance of this class
 * with its GetPosition() set to its nsSVGOuterSVGFrame's border/padding offset
 * keeps both nsDisplayTransform and nsIFrame::GetOffsetTo happy.
 *
 * The reason that this class inherit from nsSVGDisplayContainerFrame rather
 * than simply from nsContainerFrame is so that we can avoid having special
 * handling for these inner wrappers in multiple parts of the SVG code. For
 * example, the implementations of IsSVGTransformed and GetCanvasTM assume
 * nsSVGContainerFrame instances all the way up to the nsSVGOuterSVGFrame.
 */
class nsSVGOuterSVGAnonChildFrame
  : public nsSVGOuterSVGAnonChildFrameBase
{
  friend nsIFrame*
  NS_NewSVGOuterSVGAnonChildFrame(nsIPresShell* aPresShell,
                                  nsStyleContext* aContext);

  nsSVGOuterSVGAnonChildFrame(nsStyleContext* aContext)
    : nsSVGOuterSVGAnonChildFrameBase(aContext)
  {}

public:
  NS_DECL_FRAMEARENA_HELPERS

#ifdef DEBUG
  NS_IMETHOD Init(nsIContent* aContent,
                  nsIFrame* aParent,
                  nsIFrame* aPrevInFlow);

  NS_IMETHOD GetFrameName(nsAString& aResult) const {
    return MakeFrameName(NS_LITERAL_STRING("SVGOuterSVGAnonChild"), aResult);
  }
#endif

  /**
   * Get the "type" of the frame
   *
   * @see nsGkAtoms::svgOuterSVGAnonChildFrame
   */
  virtual nsIAtom* GetType() const;

  virtual bool IsSVGTransformed(gfxMatrix *aOwnTransform,
                                gfxMatrix *aFromParentTransform) const {
    // Outer-<svg> can transform its children with viewBox, currentScale and
    // currentTranslate, but it itself is not transformed by _SVG_ transforms.
    return false;
  }

  // nsSVGContainerFrame methods:
  virtual gfxMatrix GetCanvasTM(PRUint32 aFor) {
    // GetCanvasTM returns the transform from an SVG frame to the frame's
    // nsSVGOuterSVGFrame's content box, so we do not include any x/y offset
    // set on us for any CSS border or padding on our nsSVGOuterSVGFrame.
    return static_cast<nsSVGOuterSVGFrame*>(mParent)->GetCanvasTM(aFor);
  }

  virtual bool HasChildrenOnlyTransform(gfxMatrix *aTransform) const;
};

#endif
