
examples = ['0_fgets', '1_fread', '2_readdir']

foreach example : examples

    embeded_data_o = custom_target(
        example + '_data.o',
        command: [
            c_embed_exe,
            '-a',
            'elf64-x86-64',
            '-o',
            '@OUTPUT@',
            '-t',
            '@OUTDIR@/' + example + '_tmp',
            meson.current_source_dir() / example / 'data',
        ],
        output: [example + '_data.o'],
        build_by_default: false,
    )

    example_exe = executable(
        example,
        embeded_data_o,
        files(example / 'main.c'),
        dependencies: [c_embed_dep],
        c_args: ['-DCEMBED_TRANSLATE'],
        override_options: {
            'warning_level': '3',
            'werror': true,
        },
        build_by_default: true,
    )

    run_target(
        example,
        command: [example_exe],
    )


endforeach
