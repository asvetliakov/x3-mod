#!/usr/bin/env python3
"""Import the accepted local media bytes into a fresh relocatable staging tree."""
import argparse
from pathlib import Path
import sys
import media_package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--graph-provider-record', type=Path, required=True)
    parser.add_argument('--derived-record', type=Path, required=True)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        game = args.game_dir.resolve(strict=True)
        output = args.output.absolute()
        media_package.require(not output.resolve().is_relative_to(game), 'preparation output must be outside game tree')
        record = media_package.prepare(args.graph_provider_record, args.derived_record, game, output)
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))
    print(record)
    return 0


if __name__ == '__main__':
    sys.exit(main())
