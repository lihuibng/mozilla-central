/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsDragService_h_
#define nsDragService_h_

#include "nsBaseDragService.h"

#include <Cocoa/Cocoa.h>

extern NSString* const kWildcardPboardType;
extern NSString* const kCorePboardType_url;
extern NSString* const kCorePboardType_urld;
extern NSString* const kCorePboardType_urln;

class nsDragService : public nsBaseDragService
{
public:
  nsDragService();
  virtual ~nsDragService();

  // nsIDragService
  NS_IMETHOD InvokeDragSession(nsIDOMNode *aDOMNode, nsISupportsArray * anArrayTransferables,
                               nsIScriptableRegion * aRegion, PRUint32 aActionType);
  NS_IMETHOD EndDragSession(bool aDoneDrag);

  // nsIDragSession
  NS_IMETHOD GetData(nsITransferable * aTransferable, PRUint32 aItemIndex);
  NS_IMETHOD IsDataFlavorSupported(const char *aDataFlavor, bool *_retval);
  NS_IMETHOD GetNumDropItems(PRUint32 * aNumItems);

private:

  NSImage* ConstructDragImage(nsIDOMNode* aDOMNode,
                              nsIntRect* aDragRect,
                              nsIScriptableRegion* aRegion);

  nsCOMPtr<nsISupportsArray> mDataItems; // only valid for a drag started within gecko
  NSView* mNativeDragView;
  NSEvent* mNativeDragEvent;
};

#endif // nsDragService_h_
