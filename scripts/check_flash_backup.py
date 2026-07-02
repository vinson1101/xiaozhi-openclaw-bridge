from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BACKUP_DIR = ROOT / "outputs" / "flash-backups"
FLASH_SIZE = 8 * 1024 * 1024
TOKENS = [b"xiaozhi", b"zuowei-c3-realtime-lcd", b"st7789", b"SPI2_HOST", b"ADC_CHANNEL_4", b"I2S1"]


def main() -> None:
    flashes = sorted(BACKUP_DIR.glob("*_flash.bin"))
    assert flashes, "missing local flash backup"
    flash = flashes[-1]
    assert flash.stat().st_size == FLASH_SIZE, f"unexpected flash size: {flash.stat().st_size}"

    partition = flash.with_name(flash.name.replace("_flash.bin", "_partition_table.bin"))
    assert partition.exists(), "missing partition table backup"
    assert partition.stat().st_size == 4096, f"unexpected partition table size: {partition.stat().st_size}"

    data = flash.read_bytes().lower()
    missing = [token.decode() for token in TOKENS if token.lower() not in data]
    assert not missing, f"missing expected firmware clues: {', '.join(missing)}"
    print("check_flash_backup ok")


if __name__ == "__main__":
    main()
