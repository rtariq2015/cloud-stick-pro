"""Source-export admission only; no Git mutation, network or private data."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('public_export_policy', ROOT / 'tools/export_public_project.py')
exporter = importlib.util.module_from_spec(spec); spec.loader.exec_module(exporter)


class ExportPolicyTests(unittest.TestCase):
    def test_current_guides_and_preserved_public_history_are_explicit(self):
        for path in ('docs/README.md', 'docs/TROUBLESHOOTING.md', 'docs/REPOSITORY_REVIEW.md',
                     'docs/INTEGRATED_SETUP.md', 'docs/provenance/README.md',
                     'docs/provenance/2026-11-06-source-export.json', 'docs/images/cloud-stick-overview.svg'):
            self.assertEqual(exporter.destination(path), path)

    def test_new_recovery_formats_and_private_keys_cannot_enter_source_export(self):
        for path in ('desktop/recovery.key', 'tools/example.csprec', 'tools/example.CSPREC',
                     'desktop/secret.KEY', 'artifacts/public-looking.json', 'docs/raw-operator.json'):
            self.assertIsNone(exporter.destination(path))

    def test_only_reviewed_editor_configuration_is_included(self):
        self.assertEqual(exporter.destination('.vscode/settings.json'), '.vscode/settings.json')
        self.assertIsNone(exporter.destination('.vscode/local-machine-settings.json'))
        self.assertIsNone(exporter.destination('AGENTS.md'))


if __name__ == '__main__': unittest.main()
