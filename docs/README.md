# docs/

This directory contains the Doxygen configuration for generating the project's API documentation.

---

## Viewing the documentation

The documentation is built and published automatically to GitHub Pages on every push to `main`:

**https://\<your-org\>.github.io/\<your-repo\>/**

---

## Building locally

You need [Doxygen](https://www.doxygen.nl/download.html) installed and on your `PATH`.

```bash
# From the repository root:
cd docs
doxygen Doxyfile

# Open the output in your browser:
# Linux / macOS
open html/index.html
# Windows
start html/index.html
```

The generated `html/` folder is excluded from Git (see `.gitignore`). It is rebuilt from source by GitHub Actions on every push.

---

## Files

| File | Purpose |
|------|---------|
| `Doxyfile` | Main Doxygen configuration — input paths, output format, theme options |
| `mainpage.md` | Markdown content shown on the Doxygen landing page |
| `doxygen-awesome/` | Optional custom CSS theme (add as a Git submodule if used) |

---

## Adding the doxygen-awesome theme (optional)

To apply the custom theme for extra Doxygen style marks:

```bash
git submodule add https://github.com/jothepro/doxygen-awesome-css docs/doxygen-awesome
```

Then in `Doxyfile`, set:

```
HTML_EXTRA_STYLESHEET  = docs/doxygen-awesome/doxygen-awesome.css
HTML_COLORSTYLE        = LIGHT
GENERATE_TREEVIEW      = YES
```

---

## GitHub Actions

The workflow at `.github/workflows/docs.yml` runs `doxygen docs/Doxyfile` and deploys the `html/` output to the `gh-pages` branch automatically. No manual steps are needed after the initial Pages setup in repository Settings.
