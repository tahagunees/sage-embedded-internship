import argparse

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Üç faz BLDC PC simülatörü')
    parser.add_argument('--smoke-test', action='store_true')
    args = parser.parse_args()
    from simulator.gui import launch
    launch(args.smoke_test)
