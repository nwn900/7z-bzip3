// Bz3Handler.cpp

#include "StdAfx.h"

#include "../../Common/MyBuffer.h"

#include "../../Common/ComTry.h"

#include "../Common/ProgressUtils.h"
#include "../Common/RegisterArc.h"
#include "../Common/StreamUtils.h"

#include "../Compress/CopyCoder.h"

#include "Common/DummyOutStream.h"
#include "Common/HandlerOut.h"

#include "../../../C/libbz3.h"

using namespace NWindows;

namespace NArchive {
namespace NBz3 {

static const unsigned kHeaderSize = 9;
static const Byte kSignature[kHeaderSize - 4] = { 'B', 'Z', '3', 'v', '1' };
static const UInt32 kBlockSizeMin = (UInt32)65 << 10;
static const UInt32 kBlockSizeMax = (UInt32)511 << 20;

static UInt32 GetUi32(const Byte *p)
{
  return
      (UInt32)p[0]
      | ((UInt32)p[1] << 8)
      | ((UInt32)p[2] << 16)
      | ((UInt32)p[3] << 24);
}

static void SetUi32(Byte *p, UInt32 value)
{
  p[0] = (Byte)value;
  p[1] = (Byte)(value >> 8);
  p[2] = (Byte)(value >> 16);
  p[3] = (Byte)(value >> 24);
}

static bool IsValidBlockSize(UInt32 blockSize)
{
  return blockSize >= kBlockSizeMin && blockSize <= kBlockSizeMax;
}

static UInt32 GetBz3BlockSize(const CSingleMethodProps &props)
{
  UInt64 dictSize64 = 0;
  if (props.Get_DicSize(dictSize64))
  {
    if (dictSize64 < kBlockSizeMin)
      dictSize64 = kBlockSizeMin;
    if (dictSize64 > kBlockSizeMax)
      dictSize64 = kBlockSizeMax;
    return (UInt32)dictSize64;
  }

  switch (props.GetLevel())
  {
    case 0:
    case 1:
    case 2: return 1 << 20;
    case 3:
    case 4: return 4 << 20;
    case 5: return 16 << 20;
    case 6:
    case 7: return 32 << 20;
    default: return 64 << 20;
  }
}

struct CBz3StateHolder
{
  struct bz3_state *State;

  CBz3StateHolder(): State(NULL) {}
  ~CBz3StateHolder()
  {
    if (State)
      bz3_free(State);
  }
};

Z7_CLASS_IMP_CHandler_IInArchive_3(
  IArchiveOpenSeq,
  IOutArchive,
  ISetProperties
)
  CMyComPtr<IInStream> _stream;
  CMyComPtr<ISequentialInStream> _seqStream;

  bool _isArc;
  bool _needSeekToStart;
  bool _dataAfterEnd;
  bool _needMoreInput;

  bool _packSize_Defined;
  bool _unpackSize_Defined;
  bool _numStreams_Defined;
  bool _numBlocks_Defined;

  UInt64 _packSize;
  UInt64 _unpackSize;
  UInt64 _numStreams;
  UInt64 _numBlocks;

  CSingleMethodProps _props;
};

static const Byte kProps[] =
{
  kpidSize,
  kpidPackSize
};

static const Byte kArcProps[] =
{
  kpidNumStreams,
  kpidNumBlocks
};

IMP_IInArchive_Props
IMP_IInArchive_ArcProps

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT *value))
{
  NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPhySize: if (_packSize_Defined) prop = _packSize; break;
    case kpidUnpackSize: if (_unpackSize_Defined) prop = _unpackSize; break;
    case kpidNumStreams: if (_numStreams_Defined) prop = _numStreams; break;
    case kpidNumBlocks: if (_numBlocks_Defined) prop = _numBlocks; break;
    case kpidErrorFlags:
    {
      UInt32 v = 0;
      if (!_isArc) v |= kpv_ErrorFlags_IsNotArc;
      if (_needMoreInput) v |= kpv_ErrorFlags_UnexpectedEnd;
      if (_dataAfterEnd) v |= kpv_ErrorFlags_DataAfterEnd;
      prop = v;
      break;
    }
    default: break;
  }
  prop.Detach(value);
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32 *numItems))
{
  *numItems = 1;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::GetProperty(UInt32 /* index */, PROPID propID, PROPVARIANT *value))
{
  NCOM::CPropVariant prop;
  switch (propID)
  {
    case kpidPackSize: if (_packSize_Defined) prop = _packSize; break;
    case kpidSize: if (_unpackSize_Defined) prop = _unpackSize; break;
    default: break;
  }
  prop.Detach(value);
  return S_OK;
}

API_FUNC_static_IsArc IsArc_BZip3(const Byte *p, size_t size)
{
  if (size < kHeaderSize)
    return k_IsArc_Res_NEED_MORE;
  if (memcmp(p, kSignature, sizeof(kSignature)) != 0)
    return k_IsArc_Res_NO;
  return IsValidBlockSize(GetUi32(p + sizeof(kSignature))) ?
      k_IsArc_Res_YES :
      k_IsArc_Res_NO;
}
}

Z7_COM7F_IMF(CHandler::Open(IInStream *stream, const UInt64 *, IArchiveOpenCallback *))
{
  COM_TRY_BEGIN
  Close();
  {
    Byte buf[kHeaderSize];
    RINOK(ReadStream_FALSE(stream, buf, kHeaderSize))
    if (IsArc_BZip3(buf, kHeaderSize) == k_IsArc_Res_NO)
      return S_FALSE;
    _isArc = true;
    _stream = stream;
    _seqStream = stream;
    _needSeekToStart = true;
    _numStreams = 1;
    _numStreams_Defined = true;
    UInt64 size;
    if (InStream_AtBegin_GetSize(stream, size) == S_OK)
    {
      _packSize = size;
      _packSize_Defined = true;
    }
  }
  return S_OK;
  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::OpenSeq(ISequentialInStream *stream))
{
  Close();
  _isArc = true;
  _numStreams = 1;
  _numStreams_Defined = true;
  _seqStream = stream;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::Close())
{
  _isArc = false;
  _needSeekToStart = false;
  _dataAfterEnd = false;
  _needMoreInput = false;

  _packSize_Defined = false;
  _unpackSize_Defined = false;
  _numStreams_Defined = false;
  _numBlocks_Defined = false;

  _packSize = 0;
  _unpackSize = 0;
  _numStreams = 0;
  _numBlocks = 0;

  _seqStream.Release();
  _stream.Release();
  return S_OK;
}

Z7_COM7F_IMF(CHandler::Extract(const UInt32 *indices, UInt32 numItems,
    Int32 testMode, IArchiveExtractCallback *extractCallback))
{
  COM_TRY_BEGIN
  if (numItems == 0)
    return S_OK;
  if (numItems != (UInt32)(Int32)-1 && (numItems != 1 || indices[0] != 0))
    return E_INVALIDARG;

  if (_packSize_Defined)
    RINOK(extractCallback->SetTotal(_packSize))

  Int32 opRes = NExtract::NOperationResult::kDataError;
 {
  CMyComPtr<ISequentialOutStream> realOutStream;
  const Int32 askMode = testMode ?
      NExtract::NAskMode::kTest :
      NExtract::NAskMode::kExtract;
  RINOK(extractCallback->GetStream(0, &realOutStream, askMode))
  if (!testMode && !realOutStream)
    return S_OK;

  RINOK(extractCallback->PrepareOperation(askMode))

  if (_needSeekToStart)
  {
    if (!_stream)
      return E_FAIL;
    RINOK(InStream_SeekToBegin(_stream))
  }
  else
    _needSeekToStart = true;

  CMyComPtr2_Create<ISequentialOutStream, CDummyOutStream> outStream;
  outStream->SetStream(realOutStream);
  outStream->Init();

  CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
  lps->Init(extractCallback, true);

  _dataAfterEnd = false;
  _needMoreInput = false;
  _isArc = true;

  do
  {
    Byte header[kHeaderSize];
    size_t headerSize = kHeaderSize;
    RINOK(ReadStream(_seqStream, header, &headerSize))
    if (headerSize == 0 || IsArc_BZip3(header, headerSize) == k_IsArc_Res_NO)
    {
      _isArc = false;
      opRes = NExtract::NOperationResult::kIsNotArc;
      break;
    }
    if (headerSize != kHeaderSize)
    {
      _needMoreInput = true;
      opRes = NExtract::NOperationResult::kUnexpectedEnd;
      break;
    }

    const UInt32 blockSize = GetUi32(header + sizeof(kSignature));
    const size_t bufferSize = bz3_bound((size_t)blockSize);
    CByteBuffer buffer(bufferSize);

    CBz3StateHolder stateHolder;
    stateHolder.State = bz3_new((Int32)blockSize);
    if (!stateHolder.State)
      return E_OUTOFMEMORY;

    UInt64 packSize = kHeaderSize;
    UInt64 unpackSize = 0;
    UInt64 numBlocks = 0;
    bool crcError = false;
    bool dataError = false;

    for (;;)
    {
      Byte blockHeader[8];
      size_t processed = sizeof(blockHeader);
      RINOK(ReadStream(_seqStream, blockHeader, &processed))
      if (processed == 0)
        break;
      if (processed != sizeof(blockHeader))
      {
        _needMoreInput = true;
        break;
      }

      packSize += sizeof(blockHeader);

      const UInt32 compressedSize = GetUi32(blockHeader);
      const UInt32 origSize = GetUi32(blockHeader + 4);

      if (origSize > blockSize || compressedSize > bufferSize)
      {
        dataError = true;
        break;
      }

      size_t cur = compressedSize;
      RINOK(ReadStream(_seqStream, (Byte *)buffer, &cur))
      if (cur != compressedSize)
      {
        _needMoreInput = true;
        break;
      }

      packSize += compressedSize;

      if (bz3_decode_block(stateHolder.State, (Byte *)buffer, bufferSize,
          (Int32)compressedSize, (Int32)origSize) < 0)
      {
        const int err = bz3_last_error(stateHolder.State);
        if (err == BZ3_ERR_CRC)
          crcError = true;
        else if (err == BZ3_ERR_TRUNCATED_DATA)
          _needMoreInput = true;
        else
          dataError = true;
        break;
      }

      RINOK(WriteStream(outStream, (const Byte *)buffer, origSize))

      unpackSize += origSize;
      numBlocks++;
      lps.Interface()->SetRatioInfo(&packSize, &unpackSize);
    }

    _packSize = packSize;
    _packSize_Defined = true;
    _unpackSize = unpackSize;
    _unpackSize_Defined = true;
    _numStreams = 1;
    _numStreams_Defined = true;
    _numBlocks = numBlocks;
    _numBlocks_Defined = true;

    if (_needMoreInput)
      opRes = NExtract::NOperationResult::kUnexpectedEnd;
    else if (crcError)
      opRes = NExtract::NOperationResult::kCRCError;
    else if (dataError)
      opRes = NExtract::NOperationResult::kDataError;
    else
      opRes = NExtract::NOperationResult::kOK;
  }
  while (false);
 }
  return extractCallback->SetOperationResult(opRes);
  COM_TRY_END
}

static HRESULT UpdateArchive(
    UInt64 unpackSize,
    ISequentialOutStream *outStream,
    const CSingleMethodProps &props,
    IArchiveUpdateCallback *updateCallback)
{
  CMyComPtr<ISequentialInStream> fileInStream;
  RINOK(updateCallback->GetStream(0, &fileInStream))
  if (!fileInStream)
    return S_FALSE;

  {
    Z7_DECL_CMyComPtr_QI_FROM(
        IStreamGetSize,
        streamGetSize, fileInStream)
    if (streamGetSize)
    {
      UInt64 size;
      if (streamGetSize->GetSize(&size) == S_OK)
        unpackSize = size;
    }
  }

  RINOK(updateCallback->SetTotal(unpackSize))

  CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
  lps->Init(updateCallback, true);

  const UInt32 blockSize = GetBz3BlockSize(props);
  const size_t bufferSize = bz3_bound((size_t)blockSize);
  CByteBuffer buffer(bufferSize);

  CBz3StateHolder stateHolder;
  stateHolder.State = bz3_new((Int32)blockSize);
  if (!stateHolder.State)
    return E_OUTOFMEMORY;

  Byte header[kHeaderSize];
  memcpy(header, kSignature, sizeof(kSignature));
  SetUi32(header + sizeof(kSignature), blockSize);
  RINOK(WriteStream(outStream, header, sizeof(header)))

  UInt64 inSize = 0;
  UInt64 outSize = sizeof(header);

  for (;;)
  {
    size_t cur = blockSize;
    RINOK(ReadStream(fileInStream, (Byte *)buffer, &cur))
    if (cur == 0)
      break;

    const Int32 compressedSize = bz3_encode_block(stateHolder.State,
        (Byte *)buffer, (Int32)cur);
    if (compressedSize < 0)
      return E_FAIL;

    Byte blockHeader[8];
    SetUi32(blockHeader, (UInt32)compressedSize);
    SetUi32(blockHeader + 4, (UInt32)cur);
    RINOK(WriteStream(outStream, blockHeader, sizeof(blockHeader)))
    RINOK(WriteStream(outStream, (const Byte *)buffer, (UInt32)compressedSize))

    inSize += cur;
    outSize += sizeof(blockHeader) + (UInt32)compressedSize;
    lps.Interface()->SetRatioInfo(&inSize, &outSize);
  }

  return updateCallback->SetOperationResult(NArchive::NUpdate::NOperationResult::kOK);
}

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32 *timeType))
{
  *timeType = GET_FileTimeType_NotDefined_for_GetFileTimeType;
  return S_OK;
}

Z7_COM7F_IMF(CHandler::UpdateItems(ISequentialOutStream *outStream, UInt32 numItems,
    IArchiveUpdateCallback *updateCallback))
{
  COM_TRY_BEGIN

  if (numItems != 1)
    return E_INVALIDARG;

  {
    Z7_DECL_CMyComPtr_QI_FROM(
        IStreamSetRestriction,
        setRestriction, outStream)
    if (setRestriction)
      RINOK(setRestriction->SetRestriction(0, 0))
  }

  Int32 newData, newProps;
  UInt32 indexInArchive;
  if (!updateCallback)
    return E_FAIL;
  RINOK(updateCallback->GetUpdateItemInfo(0, &newData, &newProps, &indexInArchive))

  if (IntToBool(newProps))
  {
    NCOM::CPropVariant prop;
    RINOK(updateCallback->GetProperty(0, kpidIsDir, &prop))
    if (prop.vt != VT_EMPTY)
      if (prop.vt != VT_BOOL || prop.boolVal != VARIANT_FALSE)
        return E_INVALIDARG;
  }

  if (IntToBool(newData))
  {
    UInt64 size;
    NCOM::CPropVariant prop;
    RINOK(updateCallback->GetProperty(0, kpidSize, &prop))
    if (prop.vt != VT_UI8)
      return E_INVALIDARG;
    size = prop.uhVal.QuadPart;
    return UpdateArchive(size, outStream, _props, updateCallback);
  }

  if (indexInArchive != 0)
    return E_INVALIDARG;

  CMyComPtr2_Create<ICompressProgressInfo, CLocalProgress> lps;
  lps->Init(updateCallback, true);

  Z7_DECL_CMyComPtr_QI_FROM(
      IArchiveUpdateCallbackFile,
      opCallback, updateCallback)
  if (opCallback)
  {
    RINOK(opCallback->ReportOperation(
        NEventIndexType::kInArcIndex, 0,
        NUpdateNotifyOp::kReplicate))
  }

  if (_stream)
    RINOK(InStream_SeekToBegin(_stream))

  return NCompress::CopyStream(_stream, outStream, lps);

  COM_TRY_END
}

Z7_COM7F_IMF(CHandler::SetProperties(const wchar_t * const *names,
    const PROPVARIANT *values, UInt32 numProps))
{
  return _props.SetProperties(names, values, numProps);
}

REGISTER_ARC_IO(
  "bzip3", "bz3 bzip3 tbz3", "* * .tar", 5,
  kSignature,
  0,
  NArcInfoFlags::kKeepName
  , 0
  , IsArc_BZip3)

}}
