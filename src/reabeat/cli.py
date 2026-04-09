"""CLI entry point for REABeat."""

import logging

import click

from reabeat import __version__


@click.group()
@click.version_option(__version__)
def main() -> None:
    """REABeat — Neural beat detection and tempo mapping for REAPER."""


@main.command()
@click.option("--port", default=9877, help="TCP port (default: 9877)")
@click.option("--idle-timeout", default=300, help="Seconds before auto-shutdown (default: 300)")
@click.option("--verbose", is_flag=True, help="Enable debug logging")
def serve(port: int, idle_timeout: int, verbose: bool) -> None:
    """Start the REABeat analysis server."""
    level = logging.DEBUG if verbose else logging.INFO
    logging.basicConfig(
        level=level,
        format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
        datefmt="%H:%M:%S",
    )

    from reabeat.detector import check_backend
    ok, msg = check_backend()
    if not ok:
        click.echo(f"ERROR: {msg}", err=True)
        raise SystemExit(1)

    click.echo(f"Backend: {msg}")

    from reabeat.server import REABeatServer
    server = REABeatServer(port=port, idle_timeout=idle_timeout)
    server.serve()


@main.command()
@click.argument("audio_path")
def detect(audio_path: str) -> None:
    """Detect beats in an audio file (CLI mode)."""
    from reabeat.detector import check_backend, detect_beats

    ok, msg = check_backend()
    if not ok:
        click.echo(f"ERROR: {msg}", err=True)
        raise SystemExit(1)

    result = detect_beats(
        audio_path,
        on_progress=lambda msg, frac: click.echo(f"  {msg}"),
    )
    click.echo(f"\nBackend:    {result.backend}")
    click.echo(f"Tempo:      {result.tempo} BPM")
    click.echo(f"Time sig:   {result.time_sig_num}/{result.time_sig_denom}")
    click.echo(f"Beats:      {len(result.beats)}")
    click.echo(f"Downbeats:  {len(result.downbeats)}")
    click.echo(f"Confidence: {result.confidence:.1%}")
    click.echo(f"Duration:   {result.duration:.1f}s")
    click.echo(f"Time:       {result.detection_time:.1f}s")


@main.command()
def check() -> None:
    """Check if beat-this backend is installed and working."""
    from reabeat.detector import check_backend
    ok, msg = check_backend()
    if ok:
        click.echo(f"OK: {msg}")
    else:
        click.echo(f"ERROR:\n{msg}", err=True)
        raise SystemExit(1)
