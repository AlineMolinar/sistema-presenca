# Sistema de presença 

**Sistema de presença utilizando um sensor RFID (para marcar a presença por meio da leitura do cartão) em um display ESP32-S3 4.3 inch 800*400 IPS com Touch, em que o professor pode marcar as presenças e editá-las posteriormente, tem acesso a todas a suas turmas e a estatística de presença de cada turma.**

A interface consiste em uma tela de login e caso não haja professor cadastrado com a carteirinha aproximada, é renderizado a tela de cadastro do professor. Após isso, o professor tem acesso às suas turmas, e em cada turma há três abas; a aba atual, consiste na chamada do dia, os alunos passam a carteirinha e marcam a presença, na aba passadas, o professor consegue editar as presenças de cada aluno nas aulas passadas, e a aba cadastro, em que é cadastrado a carteirinha do aluno. Além disso, tem-se a parte da conta do professor, em que ele tem acesso as suas próprias informações e pode cadastrar um novo cartão, ademais ele tem acesso as estatíticas de presença de cada turma, clicando no botão de sair, ele é redirecionado para a tela de login 
Os dados utilizados estão presentes em um cartão microSD.

*Hardware utilizado: \n
Display: Sunton ESP32-S3 4.3 inch 800*400 IPS with Touch \n
Sensor: Módulo Leitor NFC/RFID PN532 V3
