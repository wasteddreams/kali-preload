# Contributing to Preheat

Thank you for considering contributing to preheat! 🎉

## How to Contribute

### Reporting Bugs
- Use the bug report template
- Include system information and logs
- Provide clear reproduction steps

### Feature Requests
- Use the feature request template
- Explain the use case clearly
- Consider implementation impact

### Pull Requests

1. **Fork and clone** the repository
2. **Create a branch**: `git checkout -b feature/your-feature`
3. **Make changes** following the coding style
4. **Test thoroughly**:
   ```bash
   make clean
   ./configure
   make -j$(nproc)
   sudo make install
   ./src/preheat --self-test
   ```
5. **Commit** with clear messages
6. **Push** and create a PR

### Coding Guidelines

- **Language**: C (C99 standard)
- **Style**: Follow existing code formatting
- **Comments**: Clear, concise explanations
- **Error handling**: Always check return values
- **Memory**: No leaks, proper cleanup
- **Compatibility**: Maintain preload 0.6.4 behavioral parity

### Building from Source

```bash
autoreconf --install --force
./configure
make -j$(nproc)
sudo make install
```

### Testing

Run the self-test suite:
```bash
./src/preheat --self-test
```

Integration tests:
```bash
cd tests/integration
./smoke_test.sh
```

### Documentation

Update relevant documentation:
- `README.md` - Quick overview
- `docs/` - Detailed guides
- `man/` - Man pages

## Questions?

Open an issue or discussion on GitHub.

## License

By contributing, you agree that your contributions will be licensed under GPL v2.
