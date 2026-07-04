"""High-level page push orchestration over the tlsr-layout GATT service."""

import asyncio
import logging

from ..ble_encode import EncodedPage, chunk_payload, encode_page
from ..ble_protocol import (
    ACK_ERR_BUSY,
    ACK_ERR_CRC,
    ACK_ERR_FLASH,
    ACK_ERR_FORMAT,
    ACK_ERR_GAP,
    ACK_ERR_INFLATE,
    ACK_ERR_LOW_BATT,
    ACK_ERR_OVERRUN,
    ACK_ERR_PARSE,
    ACK_ERR_RENDER,
    ACK_ERR_TOO_BIG,
    ACK_OK_BEGIN,
    ACK_OK_COMMIT,
    ACK_OK_PROGRESS,
    DSL_FORMAT_ZLIB_JSON_V1,
    Ack,
    build_abort,
    build_begin,
    build_commit,
    build_data_chunk,
    parse_ack,
)
from .transport import Transport

log = logging.getLogger(__name__)


class PushError(RuntimeError):
    def __init__(self, code: int, message: str) -> None:
        code_text = f"0x{code:02X}" if code >= 0 else "none"
        super().__init__(f"push failed: {message} (code {code_text})")
        self.code = code


async def push_page(
    transport: Transport,
    page: dict,
    *,
    current_data_ver: int | None = None,
    chunk_payload_size: int = 180,
    ack_timeout_s: float = 5.0,
    commit_timeout_s: float = 45.0,
    crc_retries: int = 1,
) -> EncodedPage:
    """Push one page. Recovers GAPs in-place and retries once on CRC failure.

    Content-addressed dedup: when ``current_data_ver`` (e.g. the value the tag
    advertises) equals this page's ``data_ver``, the tag already displays this
    content, so the transfer — and the tag's expensive full EPD refresh — is
    skipped entirely. Mirrors OpenEPaperLink's "already have it" short-circuit.
    """

    encoded = encode_page(page)
    if current_data_ver is not None and current_data_ver == encoded.data_ver:
        log.info("data_ver 0x%08X unchanged, skipping push", encoded.data_ver)
        return encoded

    chunks = chunk_payload(encoded.compressed, max_payload=chunk_payload_size)

    for attempt in range(crc_retries + 1):
        ack = await _transfer_once(
            transport,
            encoded=encoded,
            chunks=chunks,
            ack_timeout_s=ack_timeout_s,
            commit_timeout_s=commit_timeout_s,
        )
        if ack.code == ACK_OK_COMMIT:
            return encoded
        await transport.write_ctl(build_abort())
        if ack.code != ACK_ERR_CRC:
            raise _push_error_for_ack(ack, phase="COMMIT")
        if attempt >= crc_retries:
            raise PushError(ack.code, "CRC mismatch after retry")
        log.info("ERR_CRC, restarting transfer")

    raise AssertionError("unreachable")


async def _transfer_once(
    transport: Transport,
    *,
    encoded: EncodedPage,
    chunks: list[tuple[int, bytes]],
    ack_timeout_s: float,
    commit_timeout_s: float,
) -> Ack:
    await transport.write_ctl(
        build_begin(
            data_ver=encoded.data_ver,
            total_len=encoded.compressed_len,
            total_chunks=len(chunks),
            crc32=encoded.crc32,
            dsl_format=DSL_FORMAT_ZLIB_JSON_V1,
        )
    )
    try:
        ack = parse_ack(await transport.next_ack(ack_timeout_s))
    except TimeoutError as exc:
        raise PushError(-1, f"BEGIN ack timeout after {ack_timeout_s:g}s") from exc
    if ack.code != ACK_OK_BEGIN:
        await transport.write_ctl(build_abort())
        raise _push_error_for_ack(ack, phase="BEGIN")

    index = 0
    while index < len(chunks):
        seq, payload = chunks[index]
        await transport.write_data(build_data_chunk(seq=seq, payload=payload))
        index += 1

        if (ack := await _maybe_ack(transport)) is None:
            continue
        if ack.code in {ACK_OK_PROGRESS, ACK_OK_BEGIN}:
            continue
        if ack.code == ACK_ERR_GAP:
            index = ack.last_seq + 1
            log.info("ERR_GAP, resuming from seq %d", index)
            continue
        raise _push_error_for_ack(ack, phase="DATA")

    await transport.write_ctl(build_commit(data_ver=encoded.data_ver))
    return await _wait_for_commit_ack(transport, timeout_s=commit_timeout_s)


async def _wait_for_commit_ack(transport: Transport, *, timeout_s: float) -> Ack:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout_s
    while True:
        remaining_s = deadline - loop.time()
        if remaining_s <= 0:
            raise TimeoutError
        ack = parse_ack(await transport.next_ack(remaining_s))
        if ack.code in {ACK_OK_PROGRESS, ACK_OK_BEGIN}:
            continue
        return ack


async def _maybe_ack(transport: Transport) -> Ack | None:
    try:
        return parse_ack(await transport.next_ack(0.001))
    except TimeoutError:
        return None


def _push_error_for_ack(ack: Ack, *, phase: str) -> PushError:
    if ack.code == ACK_ERR_LOW_BATT:
        return PushError(ack.code, "low battery")
    if ack.code == ACK_ERR_CRC:
        return PushError(ack.code, "CRC mismatch")
    if ack.code == ACK_ERR_GAP:
        return PushError(ack.code, f"{phase} gap")
    if ack.code == ACK_ERR_TOO_BIG:
        return PushError(ack.code, "payload too big")
    if ack.code == ACK_ERR_FORMAT:
        return PushError(ack.code, "wire format rejected")
    if ack.code == ACK_ERR_OVERRUN:
        return PushError(ack.code, "payload overrun")
    if ack.code == ACK_ERR_FLASH:
        return PushError(ack.code, "flash write failed")
    if ack.code == ACK_ERR_INFLATE:
        return PushError(ack.code, "inflate failed")
    if ack.code == ACK_ERR_PARSE:
        return PushError(ack.code, "layout parse failed")
    if ack.code == ACK_ERR_RENDER:
        return PushError(ack.code, "render failed")
    if ack.code == ACK_ERR_BUSY:
        return PushError(ack.code, "tag busy")
    return PushError(ack.code, f"unexpected {phase} response")
