/* vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Helpers.h"
#include "mozIStorageError.h"
#include "plbase64.h"
#include "prio.h"
#include "nsString.h"
#include "nsNavHistory.h"
#include "mozilla/Services.h"
#if defined(XP_OS2)
#include "nsIRandomGenerator.h"
#endif

// The length of guids that are used by history and bookmarks.
#define GUID_LENGTH 12

namespace mozilla {
namespace places {

////////////////////////////////////////////////////////////////////////////////
//// AsyncStatementCallback

NS_IMPL_ISUPPORTS1(
  AsyncStatementCallback
, mozIStorageStatementCallback
)

NS_IMETHODIMP
AsyncStatementCallback::HandleResult(mozIStorageResultSet *aResultSet)
{
  NS_ABORT_IF_FALSE(false, "Was not expecting a resultset, but got it.");
  return NS_OK;
}

NS_IMETHODIMP
AsyncStatementCallback::HandleCompletion(PRUint16 aReason)
{
  return NS_OK;
}

NS_IMETHODIMP
AsyncStatementCallback::HandleError(mozIStorageError *aError)
{
#ifdef DEBUG
  PRInt32 result;
  nsresult rv = aError->GetResult(&result);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCAutoString message;
  rv = aError->GetMessage(message);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString warnMsg;
  warnMsg.Append("An error occurred while executing an async statement: ");
  warnMsg.AppendInt(result);
  warnMsg.Append(" ");
  warnMsg.Append(message);
  NS_WARNING(warnMsg.get());
#endif

  return NS_OK;
}

#define URI_TO_URLCSTRING(uri, spec) \
  nsCAutoString spec; \
  if (NS_FAILED(aURI->GetSpec(spec))) { \
    return NS_ERROR_UNEXPECTED; \
  }

// Bind URI to statement by index.
nsresult // static
URIBinder::Bind(mozIStorageStatement* aStatement,
                PRInt32 aIndex,
                nsIURI* aURI)
{
  NS_ASSERTION(aStatement, "Must have non-null statement");
  NS_ASSERTION(aURI, "Must have non-null uri");

  URI_TO_URLCSTRING(aURI, spec);
  return URIBinder::Bind(aStatement, aIndex, spec);
}

// Statement URLCString to statement by index.
nsresult // static
URIBinder::Bind(mozIStorageStatement* aStatement,
                PRInt32 index,
                const nsACString& aURLString)
{
  NS_ASSERTION(aStatement, "Must have non-null statement");
  return aStatement->BindUTF8StringByIndex(
    index, StringHead(aURLString, URI_LENGTH_MAX)
  );
}

// Bind URI to statement by name.
nsresult // static
URIBinder::Bind(mozIStorageStatement* aStatement,
                const nsACString& aName,
                nsIURI* aURI)
{
  NS_ASSERTION(aStatement, "Must have non-null statement");
  NS_ASSERTION(aURI, "Must have non-null uri");

  URI_TO_URLCSTRING(aURI, spec);
  return URIBinder::Bind(aStatement, aName, spec);
}

// Bind URLCString to statement by name.
nsresult // static
URIBinder::Bind(mozIStorageStatement* aStatement,
                const nsACString& aName,
                const nsACString& aURLString)
{
  NS_ASSERTION(aStatement, "Must have non-null statement");
  return aStatement->BindUTF8StringByName(
    aName, StringHead(aURLString, URI_LENGTH_MAX)
  );
}

// Bind URI to params by index.
nsresult // static
URIBinder::Bind(mozIStorageBindingParams* aParams,
                PRInt32 aIndex,
                nsIURI* aURI)
{
  NS_ASSERTION(aParams, "Must have non-null statement");
  NS_ASSERTION(aURI, "Must have non-null uri");

  URI_TO_URLCSTRING(aURI, spec);
  return URIBinder::Bind(aParams, aIndex, spec);
}

// Bind URLCString to params by index.
nsresult // static
URIBinder::Bind(mozIStorageBindingParams* aParams,
                PRInt32 index,
                const nsACString& aURLString)
{
  NS_ASSERTION(aParams, "Must have non-null statement");
  return aParams->BindUTF8StringByIndex(
    index, StringHead(aURLString, URI_LENGTH_MAX)
  );
}

// Bind URI to params by name.
nsresult // static
URIBinder::Bind(mozIStorageBindingParams* aParams,
                const nsACString& aName,
                nsIURI* aURI)
{
  NS_ASSERTION(aParams, "Must have non-null params array");
  NS_ASSERTION(aURI, "Must have non-null uri");

  URI_TO_URLCSTRING(aURI, spec);
  return URIBinder::Bind(aParams, aName, spec);
}

// Bind URLCString to params by name.
nsresult // static
URIBinder::Bind(mozIStorageBindingParams* aParams,
                const nsACString& aName,
                const nsACString& aURLString)
{
  NS_ASSERTION(aParams, "Must have non-null params array");

  nsresult rv = aParams->BindUTF8StringByName(
    aName, StringHead(aURLString, URI_LENGTH_MAX)
  );
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

#undef URI_TO_URLCSTRING

nsresult
GetReversedHostname(nsIURI* aURI, nsString& aRevHost)
{
  nsCAutoString forward8;
  nsresult rv = aURI->GetHost(forward8);
  // Not all URIs have a host.
  if (NS_FAILED(rv))
    return rv;

  // can't do reversing in UTF8, better use 16-bit chars
  GetReversedHostname(NS_ConvertUTF8toUTF16(forward8), aRevHost);
  return NS_OK;
}

void
GetReversedHostname(const nsString& aForward, nsString& aRevHost)
{
  ReverseString(aForward, aRevHost);
  aRevHost.Append(PRUnichar('.'));
}

void
ReverseString(const nsString& aInput, nsString& aReversed)
{
  aReversed.Truncate(0);
  for (PRInt32 i = aInput.Length() - 1; i >= 0; i--) {
    aReversed.Append(aInput[i]);
  }
}

static
nsresult
Base64urlEncode(const PRUint8* aBytes,
                PRUint32 aNumBytes,
                nsCString& _result)
{
  // SetLength does not set aside space for NULL termination.  PL_Base64Encode
  // will not NULL terminate, however, nsCStrings must be NULL terminated.  As a
  // result, we set the capacity to be one greater than what we need, and the
  // length to our desired length.
  PRUint32 length = (aNumBytes + 2) / 3 * 4; // +2 due to integer math.
  NS_ENSURE_TRUE(_result.SetCapacity(length + 1, fallible_t()),
                 NS_ERROR_OUT_OF_MEMORY);
  _result.SetLength(length);
  (void)PL_Base64Encode(reinterpret_cast<const char*>(aBytes), aNumBytes,
                        _result.BeginWriting());

  // base64url encoding is defined in RFC 4648.  It replaces the last two
  // alphabet characters of base64 encoding with '-' and '_' respectively.
  _result.ReplaceChar('+', '-');
  _result.ReplaceChar('/', '_');
  return NS_OK;
}

#ifdef XP_WIN
// Included here because windows.h conflicts with the use of mozIStorageError
// above.
#include <windows.h>
#include <wincrypt.h>
#endif

static
nsresult
GenerateRandomBytes(PRUint32 aSize,
                    PRUint8* _buffer)
{
  // On Windows, we'll use its built-in cryptographic API.
#if defined(XP_WIN)
  HCRYPTPROV cryptoProvider;
  BOOL rc = CryptAcquireContext(&cryptoProvider, 0, 0, PROV_RSA_FULL,
                                CRYPT_VERIFYCONTEXT | CRYPT_SILENT);
  if (rc) {
    rc = CryptGenRandom(cryptoProvider, aSize, _buffer);
    (void)CryptReleaseContext(cryptoProvider, 0);
  }
  return rc ? NS_OK : NS_ERROR_FAILURE;

  // On Unix, we'll just read in from /dev/urandom.
#elif defined(XP_UNIX)
  NS_ENSURE_ARG_MAX(aSize, PR_INT32_MAX);
  PRFileDesc* urandom = PR_Open("/dev/urandom", PR_RDONLY, 0);
  nsresult rv = NS_ERROR_FAILURE;
  if (urandom) {
    PRInt32 bytesRead = PR_Read(urandom, _buffer, aSize);
    if (bytesRead == static_cast<PRInt32>(aSize)) {
      rv = NS_OK;
    }
    (void)PR_Close(urandom);
  }
  return rv;
#elif defined(XP_OS2)
  nsCOMPtr<nsIRandomGenerator> rg =
    do_GetService("@mozilla.org/security/random-generator;1");
  NS_ENSURE_STATE(rg);

  PRUint8* temp;
  nsresult rv = rg->GenerateRandomBytes(aSize, &temp);
  NS_ENSURE_SUCCESS(rv, rv);
  memcpy(_buffer, temp, aSize);
  NS_Free(temp);
  return NS_OK;
#endif
}

nsresult
GenerateGUID(nsCString& _guid)
{
  _guid.Truncate();

  // Request raw random bytes and base64url encode them.  For each set of three
  // bytes, we get one character.
  const PRUint32 kRequiredBytesLength =
    static_cast<PRUint32>(GUID_LENGTH / 4 * 3);

  PRUint8 buffer[kRequiredBytesLength];
  nsresult rv = GenerateRandomBytes(kRequiredBytesLength, buffer);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = Base64urlEncode(buffer, kRequiredBytesLength, _guid);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ASSERTION(_guid.Length() == GUID_LENGTH, "GUID is not the right size!");
  return NS_OK;
}

bool
IsValidGUID(const nsCString& aGUID)
{
  nsCString::size_type len = aGUID.Length();
  if (len != GUID_LENGTH) {
    return false;
  }

  for (nsCString::size_type i = 0; i < len; i++ ) {
    char c = aGUID[i];
    if ((c >= 'a' && c <= 'z') || // a-z
        (c >= 'A' && c <= 'Z') || // A-Z
        (c >= '0' && c <= '9') || // 0-9
        c == '-' || c == '_') { // - or _
      continue;
    }
    return false;
  }
  return true;
}

void
TruncateTitle(const nsACString& aTitle, nsACString& aTrimmed)
{
  aTrimmed = aTitle;
  if (aTitle.Length() > TITLE_LENGTH_MAX) {
    aTrimmed = StringHead(aTitle, TITLE_LENGTH_MAX);
  }
}

void
ForceWALCheckpoint()
{
  nsRefPtr<Database> DB = Database::GetDatabase();
  if (DB) {
    nsCOMPtr<mozIStorageAsyncStatement> stmt = DB->GetAsyncStatement(
      "pragma wal_checkpoint "
    );
    if (stmt) {
      nsCOMPtr<mozIStoragePendingStatement> handle;
      (void)stmt->ExecuteAsync(nsnull, getter_AddRefs(handle));
    }
  }
}

bool
GetHiddenState(bool aIsRedirect,
               PRUint32 aTransitionType)
{
  return aTransitionType == nsINavHistoryService::TRANSITION_FRAMED_LINK ||
         aTransitionType == nsINavHistoryService::TRANSITION_EMBED ||
         aIsRedirect;
}

////////////////////////////////////////////////////////////////////////////////
//// PlacesEvent

PlacesEvent::PlacesEvent(const char* aTopic)
: mTopic(aTopic)
{
}

NS_IMETHODIMP
PlacesEvent::Run()
{
  Notify();
  return NS_OK;
}

void
PlacesEvent::Notify()
{
  NS_ASSERTION(NS_IsMainThread(), "Must only be used on the main thread!");
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    (void)obs->NotifyObservers(nsnull, mTopic, nsnull);
  }
}

NS_IMPL_THREADSAFE_ISUPPORTS1(
  PlacesEvent
, nsIRunnable
)

////////////////////////////////////////////////////////////////////////////////
//// AsyncStatementCallbackNotifier

NS_IMETHODIMP
AsyncStatementCallbackNotifier::HandleCompletion(PRUint16 aReason)
{
  if (aReason != mozIStorageStatementCallback::REASON_FINISHED)
    return NS_ERROR_UNEXPECTED;

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    (void)obs->NotifyObservers(nsnull, mTopic, nsnull);
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
//// AsyncStatementCallbackNotifier

NS_IMETHODIMP
AsyncStatementTelemetryTimer::HandleCompletion(PRUint16 aReason)
{
  if (aReason == mozIStorageStatementCallback::REASON_FINISHED) {
    Telemetry::AccumulateTimeDelta(mHistogramId, mStart);
  }
  return NS_OK;
}

} // namespace places
} // namespace mozilla
